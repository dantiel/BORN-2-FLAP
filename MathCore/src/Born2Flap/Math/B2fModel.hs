-- | The @.b2f-model@ exchange format: a serialized 'WingModel' plus the visual
-- and solver sections that make a complete Born2Flap bird portable.
--
-- A model in Born2Flap is ALREADY pure data (see Born2Flap.Math.Model): a
-- 'WingShape' outline, a 'SparInventory' rod list and a dry mass. This module
-- makes that data portable by (de)serialising it to a JSON manifest and by
-- packaging it as a folder — the @.b2f-model@ package:
--
-- > mybird.b2f-model/
-- >   manifest.json      -- physics manifest + visual references + solver list
-- >   art/…              -- wing.svg, textures, mesh sources (referenced, not parsed)
--
-- The Haskell side OWNS and interprets the physics sections (@name@, @mass_kg@,
-- @wing@, @spars@). The @visual@ section is carried as an opaque JSON value for
-- the Unreal host to consume (materials, mesh sources, procedural generators);
-- the @solvers@ list names which hard-coded solver modules apply to this bird.
-- The format is versioned so the parser can migrate old files, and the ABI is
-- additive so old hosts simply never call the new functions.
module Born2Flap.Math.B2fModel
  ( ModelManifest(..)
  , b2fFormatTag
  , b2fFormatVersion
    -- * Manifest <-> WingModel
  , encodeManifest
  , decodeManifest
  , encodeWingModel
  , decodeWingModel
  ) where

import Born2Flap.Math.Json
import Born2Flap.Math.Model (WingModel(..))
import Born2Flap.Math.Planform (WingShape(..), ShapeStation(..))
import Born2Flap.Math.Section (SectionProfile(..))
import Born2Flap.Math.Structure
  ( Spar(..), SparKind(..), SparInventory(..), StructureProfile(..) )

b2fFormatTag :: String
b2fFormatTag = "born2flap.model"

b2fFormatVersion :: Int
b2fFormatVersion = 1

-- | The complete manifest: a named bird (physics) plus its visual blob and the
-- list of solver modules that apply to it.
data ModelManifest = ModelManifest
  { mmName    :: String
  , mmModel   :: WingModel
  , mmVisual  :: JValue     -- ^ opaque; consumed by the Unreal host
  , mmSolvers :: [String]   -- ^ solver modules that apply (informational)
  } deriving (Eq, Show)

-- ── Encode ─────────────────────────────────────────────────────────────────

encodeManifest :: ModelManifest -> JValue
encodeManifest mm = JObject
  ( [ ("format", JString b2fFormatTag)
    , ("version", JNumber (fromIntegral b2fFormatVersion))
    ]
    ++ encodeWingModelFields (mmModel mm)
    ++ [ ("visual", mmVisual mm)
       , ("solvers", JArray (map JString (mmSolvers mm)))
       ]
  )

encodeWingModel :: WingModel -> JValue
encodeWingModel = JObject . encodeWingModelFields

encodeWingModelFields :: WingModel -> [(String, JValue)]
encodeWingModelFields wm =
  [ ("name", JString (wmName wm))
  , ("mass_kg", JNumber (wmMassKg wm))
  , ("wing", encodeWing (wmWing wm))
  , ("spars", encodeSpars (wmSpars wm))
  ]

encodeWing :: WingShape -> JValue
encodeWing ws = JObject
  [ ("span_m", JNumber (wsSpanM ws))
  , ("shoulder_m", JNumber (wsShoulderM ws))
  , ("mount_angle_deg", JNumber (wsMountAngleDeg ws))
  , ("arm_angle_deg", JNumber (wsArmAngleDeg ws))
  , ("stations", JArray (map encodeStation (wsStations ws)))
  ]

encodeStation :: ShapeStation -> JValue
encodeStation st = JObject
  [ ("fraction", JNumber (ssFraction st))
  , ("lead_x_m", JNumber (ssLeadX st))
  , ("chord_m", JNumber (ssChordM st))
  , ("twist_deg", JNumber (ssTwistDeg st))
  , ("dihedral_deg", JNumber (ssDihedDeg st))
  , ("section", encodeSection (ssSection st))
  , ("structure", encodeStructure (ssStructure st))
  ]

encodeSection :: SectionProfile -> JValue
encodeSection sp = JObject
  [ ("rest_camber", JNumber (spRestCamber sp))
  , ("reflex", JNumber (spReflex sp))
  , ("membrane_k", JNumber (spMembraneK sp))
  , ("membrane_tau", JNumber (spMembraneTau sp))
  ]

encodeStructure :: StructureProfile -> JValue
encodeStructure st = JObject
  [ ("bend_ei", JNumber (stBendEI st))
  , ("twist_gj", JNumber (stTwistGJ st))
  , ("tau_bend", JNumber (stTauBend st))
  , ("tau_twist", JNumber (stTauTwist st))
  ]

encodeSpars :: SparInventory -> JValue
encodeSpars inv = JObject
  [ ("youngs_modulus_pa", JNumber (siYoungsModulus inv))
  , ("shear_modulus_pa", JNumber (siShearModulus inv))
  , ("bend_scale", JNumber (siBendScale inv))
  , ("twist_scale", JNumber (siTwistScale inv))
  , ("membrane_gj", JNumber (siMembraneGJ inv))
  , ("brace_scale", JNumber (siBraceScale inv))
  , ("rods", JArray (map encodeRod (siSpars inv)))
  ]

encodeRod :: Spar -> JValue
encodeRod sp = JObject
  [ ("name", JString (spName sp))
  , ("kind", JString (case spKind sp of SparMain -> "main"; SparBrace -> "brace"))
  , ("diameter_m", JNumber (spDiameter sp))
  , ("length_m", JNumber (spLengthM sp))
  , ("lever_m", JNumber (spLeverM sp))
  , ("root_frac", JNumber (spRootFrac sp))
  , ("tip_frac", JNumber (spTipFrac sp))
  ]

-- ── Decode ─────────────────────────────────────────────────────────────────

decodeManifest :: JValue -> Either String ModelManifest
decodeManifest value = do
  fields <- expectObject "manifest" value
  fmt <- expectString "format" =<< reqField "format" fields
  validate (fmt == b2fFormatTag) ("unknown format tag: " ++ fmt)
  ver <- expectNumber "version" =<< reqField "version" fields
  validate (round ver == b2fFormatVersion) ("unsupported version: " ++ show ver)
  model <- decodeWingModel value
  solvers <- case objectLookup "solvers" value of
    Nothing -> Right []
    Just jv -> case asArray jv of
      Just ss -> traverse (expectString "solvers") ss
      Nothing -> Left "solvers must be an array of strings"
  let visual = maybe (JObject []) id (objectLookup "visual" value)
  Right (ModelManifest (wmName model) model visual solvers)

decodeWingModel :: JValue -> Either String WingModel
decodeWingModel value = do
  fields <- expectObject "model" value
  name <- expectString "name" =<< reqField "name" fields
  mass <- expectNumber "mass_kg" =<< reqField "mass_kg" fields
  wing <- decodeWing =<< reqField "wing" fields
  spars <- decodeSpars =<< reqField "spars" fields
  Right (WingModel name wing spars mass)

decodeWing :: JValue -> Either String WingShape
decodeWing value = do
  fields <- expectObject "wing" value
  spanM <- expectNumber "span_m" =<< reqField "span_m" fields
  shoulder <- expectNumber "shoulder_m" =<< reqField "shoulder_m" fields
  mount <- expectNumber "mount_angle_deg" =<< reqField "mount_angle_deg" fields
  arm <- expectNumber "arm_angle_deg" =<< reqField "arm_angle_deg" fields
  stations <- decodeStations =<< reqField "stations" fields
  Right (WingShape spanM shoulder mount arm stations)

decodeStations :: JValue -> Either String [ShapeStation]
decodeStations (JArray xs) = traverse decodeStation xs
decodeStations _ = Left "stations must be an array"

decodeStation :: JValue -> Either String ShapeStation
decodeStation value = do
  fields <- expectObject "station" value
  frac <- expectNumber "fraction" =<< reqField "fraction" fields
  lead <- expectNumber "lead_x_m" =<< reqField "lead_x_m" fields
  chord <- expectNumber "chord_m" =<< reqField "chord_m" fields
  twist <- expectNumber "twist_deg" =<< reqField "twist_deg" fields
  dihed <- expectNumber "dihedral_deg" =<< reqField "dihedral_deg" fields
  sec <- decodeSection =<< reqField "section" fields
  struc <- decodeStructure =<< reqField "structure" fields
  Right (ShapeStation frac lead chord twist dihed sec struc)

decodeSection :: JValue -> Either String SectionProfile
decodeSection value = do
  fields <- expectObject "section" value
  camber <- expectNumber "rest_camber" =<< reqField "rest_camber" fields
  reflex <- expectNumber "reflex" =<< reqField "reflex" fields
  mk <- expectNumber "membrane_k" =<< reqField "membrane_k" fields
  tau <- expectNumber "membrane_tau" =<< reqField "membrane_tau" fields
  Right (SectionProfile camber reflex mk tau)

decodeStructure :: JValue -> Either String StructureProfile
decodeStructure value = do
  fields <- expectObject "structure" value
  ei <- expectNumber "bend_ei" =<< reqField "bend_ei" fields
  gj <- expectNumber "twist_gj" =<< reqField "twist_gj" fields
  tb <- expectNumber "tau_bend" =<< reqField "tau_bend" fields
  tt <- expectNumber "tau_twist" =<< reqField "tau_twist" fields
  Right (StructureProfile ei gj tb tt)

decodeSpars :: JValue -> Either String SparInventory
decodeSpars value = do
  fields <- expectObject "spars" value
  youngs <- expectNumber "youngs_modulus_pa" =<< reqField "youngs_modulus_pa" fields
  shear <- expectNumber "shear_modulus_pa" =<< reqField "shear_modulus_pa" fields
  bendScale <- expectNumber "bend_scale" =<< reqField "bend_scale" fields
  twistScale <- expectNumber "twist_scale" =<< reqField "twist_scale" fields
  membraneGj <- expectNumber "membrane_gj" =<< reqField "membrane_gj" fields
  braceScale <- expectNumber "brace_scale" =<< reqField "brace_scale" fields
  rods <- decodeRods =<< reqField "rods" fields
  Right (SparInventory youngs shear bendScale twistScale membraneGj braceScale rods)

decodeRods :: JValue -> Either String [Spar]
decodeRods (JArray xs) = traverse decodeRod xs
decodeRods _ = Left "rods must be an array"

decodeRod :: JValue -> Either String Spar
decodeRod value = do
  fields <- expectObject "rod" value
  name <- expectString "name" =<< reqField "name" fields
  kindVal <- expectString "kind" =<< reqField "kind" fields
  kind <- decodeSparKind kindVal
  diameter <- expectNumber "diameter_m" =<< reqField "diameter_m" fields
  rodLength <- expectNumber "length_m" =<< reqField "length_m" fields
  lever <- expectNumber "lever_m" =<< reqField "lever_m" fields
  rootFrac <- expectNumber "root_frac" =<< reqField "root_frac" fields
  tipFrac <- expectNumber "tip_frac" =<< reqField "tip_frac" fields
  Right (Spar name kind diameter rodLength lever rootFrac tipFrac)

decodeSparKind :: String -> Either String SparKind
decodeSparKind "main"  = Right SparMain
decodeSparKind "brace" = Right SparBrace
decodeSparKind other   = Left ("unknown spar kind: " ++ other)

-- ── Helpers ────────────────────────────────────────────────────────────────

reqField :: String -> [(String, JValue)] -> Either String JValue
reqField key fields =
  maybe (Left ("missing field: " ++ key)) Right (lookup key fields)

expectObject :: String -> JValue -> Either String [(String, JValue)]
expectObject _ (JObject fields) = Right fields
expectObject path _ = Left ("expected object: " ++ path)

expectNumber :: String -> JValue -> Either String Double
expectNumber _ (JNumber n) = Right n
expectNumber path _ = Left ("expected number: " ++ path)

expectString :: String -> JValue -> Either String String
expectString _ (JString s) = Right s
expectString path _ = Left ("expected string: " ++ path)

validate :: Bool -> String -> Either String ()
validate True _ = Right ()
validate False msg = Left msg