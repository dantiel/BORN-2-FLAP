-- | Runtime model catalog: the name → 'WingModel' registry that backs the
-- model-format ABI. Seeded with the five built-in models; @.b2f-model@ files
-- loaded at runtime are inserted here by name. The id-based legacy dispatch
-- (Born2Flap.Math.FFI 'modelById') stays for backward compatibility, but the
-- format ABI (load by name / select by name / enumerate) uses this catalog.
--
-- The registry is a plain assoc list (base only) — a handful of birds never
-- needs a balanced tree, and keeping @containers@ out of the foreign-library
-- link keeps the Windows shared library under the PE export-table ceiling.
module Born2Flap.Math.ModelCatalog
  ( lookupModelByName
  , insertModel
  , listModelNames
  , modelCount
  , modelNameAt
  , loadModelFromManifestText
  ) where

import Data.IORef
import Data.List (sort)
import System.IO.Unsafe (unsafePerformIO)

import Born2Flap.Math.Json (parseJson)
import Born2Flap.Math.B2fModel (ModelManifest(..), decodeManifest)
import Born2Flap.Math.Model
  ( WingModel(..), kestrelModel, kestrelLightModel, ravenModel, prototypeModel, turtleModel )

-- | The built-in models. Names are unique; later inserts with the same name
-- overwrite (a loaded @.b2f-model@ may replace a built-in).
builtins :: [WingModel]
builtins =
  [ kestrelModel
  , kestrelLightModel
  , ravenModel
  , prototypeModel
  , turtleModel
  ]

{-# NOINLINE globalCatalog #-}
globalCatalog :: IORef [(String, WingModel)]
globalCatalog = unsafePerformIO (newIORef [(wmName m, m) | m <- builtins])

lookupModelByName :: String -> IO (Maybe WingModel)
lookupModelByName name = do
  entries <- readIORef globalCatalog
  pure (lookup name entries)

insertModel :: WingModel -> IO ()
insertModel m =
  atomicModifyIORef' globalCatalog (\entries -> (insertAssoc (wmName m) m entries, ()))
  where
    insertAssoc k v [] = [(k, v)]
    insertAssoc k v ((k', v') : rest)
      | k == k'   = (k, v) : rest
      | otherwise = (k', v') : insertAssoc k v rest

-- | Catalog names in a deterministic (alphabetical) order.
listModelNames :: IO [String]
listModelNames = do
  entries <- readIORef globalCatalog
  pure (sort (map fst entries))

modelCount :: IO Int
modelCount = do
  entries <- readIORef globalCatalog
  pure (length entries)

modelNameAt :: Int -> IO (Maybe String)
modelNameAt i = do
  names <- listModelNames
  pure (if i >= 0 && i < length names then Just (names !! i) else Nothing)

-- | Parse and validate a @.b2f-model@ manifest text, then register the model by
-- name. Returns the loaded model or a parse/validation error.
loadModelFromManifestText :: String -> IO (Either String WingModel)
loadModelFromManifestText text =
  case parseJson text >>= decodeManifest of
    Left err -> pure (Left err)
    Right manifest -> do
      let model = mmModel manifest
      insertModel model
      pure (Right model)
