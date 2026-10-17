{-# LANGUAGE DerivingStrategies #-}
{-# LANGUAGE StrictData #-}

-- | A "model config": the complete physical definition of one bird — its wing
-- outline plus the carbon-rod spar inventory that bends and twists that outline
-- under load, and its dry mass. Nothing model-specific lives in the solver:
-- every aerodynamic and aeroelastic number is DERIVED from the 'WingShape',
-- 'SparInventory' and mass carried here, so a second model (different rods,
-- different wing shape, different weight) is a new 'WingModel' value, not a new
-- code path and not a magic constant.
module Born2Flap.Math.Model
  ( WingModel(..)
  , kestrelModel
  , ravenModel
  , prototypeModel
  , turtleModel
  ) where

import Born2Flap.Math.Planform (WingShape, defaultBirdWing, scaleWing)
import Born2Flap.Math.Structure (SparInventory, kestrelSpars, scaleSpars)

data WingModel = WingModel
  { wmName   :: !String        -- ^ human-readable model identifier
  , wmWing   :: !WingShape     -- ^ outline: span, shoulder, stations (with per-station section/structure)
  , wmSpars  :: !SparInventory -- ^ carbon-rod inventory driving spanwise EI/GJ
  , wmMassKg :: !Double        -- ^ dry mass of the whole bird [kg] (Chaos body mass + airframe inertia)
  } deriving stock (Eq, Show)

-- | The reference kestrel: 0.55 m half-span (1.10 m full span), 50 g dry mass,
-- 2.0 mm inner → 1.6 mm outer LE mainspar + diagonal/mid-chord brace box.
-- PTK 4765 (or 4765 Plus) servo.
kestrelModel :: WingModel
kestrelModel = WingModel "kestrel" defaultBirdWing kestrelSpars 0.050

-- | RavenCrow (nature valley): 0.66 m half-span (1.32 m full span), the kestrel
-- outline scaled 1.2×. Mass scales ~s³ (structural similarity): 50 g × 1.2³.
ravenModel :: WingModel
ravenModel = WingModel "raven" (scaleWing 1.2 defaultBirdWing) (scaleSpars 1.2 kestrelSpars) 0.086

-- | Prototype (training arena): 0.495 m half-span (0.99 m full span), the
-- kestrel outline scaled 0.9×. Mass 50 g × 0.9³ ≈ 36 g.
prototypeModel :: WingModel
prototypeModel = WingModel "prototype" (scaleWing 0.9 defaultBirdWing) (scaleSpars 0.9 kestrelSpars) 0.036

-- | Neon turtle (void level): reuses the kestrel outline; the void disables
-- gravity and uses dense-medium drag, so its mass is only a placeholder.
turtleModel :: WingModel
turtleModel = WingModel "turtle" defaultBirdWing kestrelSpars 0.050
