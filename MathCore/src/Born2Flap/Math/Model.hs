{-# LANGUAGE DerivingStrategies #-}
{-# LANGUAGE StrictData #-}

-- | A "model config": the complete physical definition of one bird — its wing
-- outline plus the carbon-rod spar inventory that bends and twists that outline
-- under load. Nothing model-specific lives in the solver: every aerodynamic and
-- aeroelastic number is DERIVED from the 'WingShape' and 'SparInventory' carried
-- here, so a second model (different rods, different wing shape) is a new
-- 'WingModel' value, not a new code path and not a magic constant.
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
  { wmName  :: !String       -- ^ human-readable model identifier
  , wmWing  :: !WingShape    -- ^ outline: span, shoulder, stations (with per-station section/structure)
  , wmSpars :: !SparInventory -- ^ carbon-rod inventory driving spanwise EI/GJ
  } deriving stock (Eq, Show)

-- | The reference kestrel: 0.55 m half-span (1.10 m full span) with the 2.0 mm
-- inner → 1.6 mm outer LE mainspar and the diagonal + mid-chord brace box.
kestrelModel :: WingModel
kestrelModel = WingModel "kestrel" defaultBirdWing kestrelSpars

-- | RavenCrow (nature valley): a larger, heavier bird — 0.66 m half-span
-- (1.32 m full span), the kestrel outline scaled 1.2×, with proportionally
-- longer and thicker carbon rods. Flies slower and heavier than the kestrel.
ravenModel :: WingModel
ravenModel = WingModel "raven" (scaleWing 1.2 defaultBirdWing) (scaleSpars 1.2 kestrelSpars)

-- | Prototype (training arena): a smaller, lighter trainer — 0.495 m half-span
-- (0.99 m full span), the kestrel outline scaled 0.9×. A lighter wing on
-- proportionally lighter rods reads stiffer, so it is easier to fly indoors.
prototypeModel :: WingModel
prototypeModel = WingModel "prototype" (scaleWing 0.9 defaultBirdWing) (scaleSpars 0.9 kestrelSpars)

-- | Neon turtle (void level): the aether is weightless and the turtle drives
-- broad flippers, not a feathered membrane wing. Until a dedicated flipper
-- planform is authored it reuses the kestrel outline; the void level's disabled
-- gravity and dense-medium drag govern feel, not this geometry.
turtleModel :: WingModel
turtleModel = WingModel "turtle" defaultBirdWing kestrelSpars