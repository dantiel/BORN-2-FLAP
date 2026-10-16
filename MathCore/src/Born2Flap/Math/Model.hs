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
  ) where

import Born2Flap.Math.Planform (WingShape, defaultBirdWing)
import Born2Flap.Math.Structure (SparInventory, kestrelSpars)

data WingModel = WingModel
  { wmName  :: !String       -- ^ human-readable model identifier
  , wmWing  :: !WingShape    -- ^ outline: span, shoulder, stations (with per-station section/structure)
  , wmSpars :: !SparInventory -- ^ carbon-rod inventory driving spanwise EI/GJ
  } deriving stock (Eq, Show)

-- | The reference kestrel: 0.55 m half-span (1.10 m full span) with the 2.0 mm
-- inner → 1.6 mm outer LE mainspar and the diagonal + mid-chord brace box.
kestrelModel :: WingModel
kestrelModel = WingModel "kestrel" defaultBirdWing kestrelSpars
