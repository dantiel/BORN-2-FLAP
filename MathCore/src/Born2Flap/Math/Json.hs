-- | A minimal, dependency-light JSON value model + parser/printer.
--
-- Born2Flap deliberately avoids an external JSON library: aeson and its
-- dependency tree do not survive the wasm / static cross-builds (see
-- Tools/build-math-wasm.sh), and even Parsec + containers tip the Windows
-- foreign-library link past the PE export-table ceiling (ld.lld caps at 65535
-- exported symbols under @--export-all-symbols@). This module is therefore
-- dependency-free (base only): a hand-rolled recursive-descent parser plus a
-- stable pretty-printer. It is the serialisation substrate for the
-- @.b2f-model@ manifest format (see Born2Flap.Math.B2fModel).
module Born2Flap.Math.Json
  ( JValue(..)
  , parseJson
  , renderJson
  , renderJsonCompact
  , objectLookup
  , asObject
  , asArray
  , asString
  , asNumber
  , asBool
  ) where

import Data.Char (chr, digitToInt, isDigit, isHexDigit, isSpace, ord)
import Data.List (intercalate)
import Numeric (showHex)
import Text.Read (readMaybe)

-- | A JSON value. Object keys are kept in insertion order (round-trip stable).
data JValue
  = JNull
  | JBool Bool
  | JNumber Double
  | JString String
  | JArray [JValue]
  | JObject [(String, JValue)]
  deriving (Eq, Show)

-- ── Parser ────────────────────────────────────────────────────────────────

parseJson :: String -> Either String JValue
parseJson input = do
  (value, rest) <- parseValue (skipWs input)
  case skipWs rest of
    []  -> Right value
    junk -> Left ("trailing input after JSON: " ++ take 24 junk)

skipWs :: String -> String
skipWs = dropWhile isSpace

parseValue :: String -> Either String (JValue, String)
parseValue input0 =
  let input = skipWs input0
  in case input of
       []        -> Left "unexpected end of JSON input"
       ('n' : r) -> parseKeyword "null" JNull r
       ('t' : r) -> parseKeyword "true" (JBool True) r
       ('f' : r) -> parseKeyword "false" (JBool False) r
       ('"' : r) -> fmap (\(s, r') -> (JString s, r')) (parseStringChars r)
       ('[' : r) -> parseArray r
       ('{' : r) -> parseObject r
       (c : _) | c == '-' || isDigit c -> parseNumber input
       (c : _) -> Left ("unexpected character: " ++ [c])

parseKeyword :: String -> a -> String -> Either String (a, String)
parseKeyword kw value rest =
  let (taken, left) = splitAt (length kw) rest
  in if taken == kw then Right (value, left) else Left ("expected " ++ kw)

parseStringChars :: String -> Either String (String, String)
parseStringChars = go []
  where
    go acc [] = Left "unterminated string"
    go acc ('"' : rest) = Right (reverse acc, rest)
    go acc ('\\' : rest) =
      case rest of
        [] -> Left "unterminated escape"
        (e : rest') -> case e of
          '"'  -> go ('"' : acc) rest'
          '\\' -> go ('\\' : acc) rest'
          '/'  -> go ('/' : acc) rest'
          'b'  -> go ('\b' : acc) rest'
          'f'  -> go ('\f' : acc) rest'
          'n'  -> go ('\n' : acc) rest'
          'r'  -> go ('\r' : acc) rest'
          't'  -> go ('\t' : acc) rest'
          'u'  -> case splitAt 4 rest' of
                    (hex, rest'') | length hex == 4 && all isHexDigit hex ->
                      go (chr (hexVal hex) : acc) rest''
                    _ -> Left "invalid unicode escape"
          _    -> Left ("invalid escape: \\" ++ [e])
    go acc (c : rest)
      | ord c >= 0x20 = go (c : acc) rest
      | otherwise     = Left "unescaped control character in string"

hexVal :: String -> Int
hexVal = foldl (\n c -> n * 16 + digitToInt c) 0

parseNumber :: String -> Either String (JValue, String)
parseNumber input =
  case span isNumberChar input of
    ([], _)   -> Left "expected number"
    (token, rest) ->
      case readMaybe token :: Maybe Double of
        Just d  -> Right (JNumber d, rest)
        Nothing -> Left ("invalid number: " ++ token)
  where
    isNumberChar c = isDigit c || c `elem` "-+.eE"

parseArray :: String -> Either String (JValue, String)
parseArray input0 =
  let input = skipWs input0
  in case input of
       (']' : rest) -> Right (JArray [], rest)
       _            -> go [] input
  where
    go acc s0 =
      let s = skipWs s0
      in do
        (value, after) <- parseValue s
        case skipWs after of
          (',' : rest) -> go (value : acc) rest
          (']' : rest) -> Right (JArray (reverse (value : acc)), rest)
          _            -> Left "expected ',' or ']' in array"

parseObject :: String -> Either String (JValue, String)
parseObject input0 =
  let input = skipWs input0
  in case input of
       ('}' : rest) -> Right (JObject [], rest)
       _            -> go [] input
  where
    go acc s0 =
      let s = skipWs s0
      in do
        (key, afterKey) <- parseStringChars s
        case skipWs afterKey of
          (':' : rest) -> do
            (value, afterVal) <- parseValue rest
            case skipWs afterVal of
              (',' : rest2) -> go ((key, value) : acc) rest2
              ('}' : rest2) -> Right (JObject (reverse ((key, value) : acc)), rest2)
              _             -> Left "expected ',' or '}' in object"
          _ -> Left "expected ':' after object key"

-- ── Printer ───────────────────────────────────────────────────────────────

-- | Pretty-printed JSON (2-space indent, multi-line for non-empty containers).
renderJson :: JValue -> String
renderJson = go 0
  where
    pad n = replicate (2 * n) ' '
    go n v = case v of
      JNull       -> "null"
      JBool True  -> "true"
      JBool False -> "false"
      JNumber x   -> renderNumber x
      JString s   -> renderString s
      JArray []   -> "[]"
      JArray xs   -> "[\n" ++ join n (map (go (n + 1)) xs) ++ "\n" ++ pad n ++ "]"
      JObject []  -> "{}"
      JObject kvs ->
        "{\n" ++ join n [renderString k ++ ": " ++ go (n + 1) val | (k, val) <- kvs]
          ++ "\n" ++ pad n ++ "}"
    join n parts = intercalate (",\n") [pad (n + 1) ++ p | p <- parts]

-- | Single-line JSON (no whitespace).
renderJsonCompact :: JValue -> String
renderJsonCompact v = case v of
  JNull       -> "null"
  JBool True  -> "true"
  JBool False -> "false"
  JNumber n   -> renderNumber n
  JString s   -> renderString s
  JArray xs   -> "[" ++ intercalate "," (map renderJsonCompact xs) ++ "]"
  JObject kvs -> "{" ++ intercalate "," [renderString k ++ ":" ++ renderJsonCompact val | (k, val) <- kvs] ++ "}"

renderNumber :: Double -> String
renderNumber n
  | isNaN n || isInfinite n = "0"
  | otherwise = show n

renderString :: String -> String
renderString s = '"' : concatMap esc s ++ "\""
  where
    esc '"'  = "\\\""
    esc '\\' = "\\\\"
    esc '\n' = "\\n"
    esc '\r' = "\\r"
    esc '\t' = "\\t"
    esc '\b' = "\\b"
    esc '\f' = "\\f"
    esc c
      | ord c < 0x20 = "\\u" ++ pad4 (showHex (ord c) "")
      | otherwise    = [c]
    pad4 h = replicate (4 - length h) '0' ++ h

-- ── Accessors ─────────────────────────────────────────────────────────────

objectLookup :: String -> JValue -> Maybe JValue
objectLookup key (JObject fields) = lookup key fields
objectLookup _ _ = Nothing

asObject :: JValue -> Maybe [(String, JValue)]
asObject (JObject fields) = Just fields
asObject _ = Nothing

asArray :: JValue -> Maybe [JValue]
asArray (JArray xs) = Just xs
asArray _ = Nothing

asString :: JValue -> Maybe String
asString (JString s) = Just s
asString _ = Nothing

asNumber :: JValue -> Maybe Double
asNumber (JNumber n) = Just n
asNumber _ = Nothing

asBool :: JValue -> Maybe Bool
asBool (JBool b) = Just b
asBool _ = Nothing
