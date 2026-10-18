{-# LANGUAGE DerivingStrategies #-}
{-# LANGUAGE StrictData #-}

-- | A "model config": the complete physical definition of one bird — its wing
-- outline plus the carbon-rod spar inventory that bends and twists that outline
-- under load, and its dry mass. Nothing model-specific lives in the solver:
-- every aerodynamic and aeroelastic number is DERIVED from the 'WingShape',
-- 'SparInventory' and mass carried here, so a second model (different rods,
-- different wing shape, different weight) is a new 'WingModel' value, not a new
-- code path and not a magic constant.
--
-- Spars are an OPEN-ENDED LIST (see 'Spar'): each bird carries its own spars
-- with individual diameter (thickness) and length, so a raven and a kestrel no
-- longer share a single scaled copy — they each own their rods. Add a spar by
-- adding a list entry; add a bird by writing a new 'WingModel'.
module Born2Flap.Math.Model
  ( WingModel(..)
  , kestrelModel
  , ravenModel
  , prototypeModel
  , turtleModel
  , kestrelLightModel
  ) where

import Born2Flap.Math.Planform (WingShape, defaultBirdWing, scaleWing)
import Born2Flap.Math.Structure
  ( Spar(..), SparKind(..), SparInventory(..), kestrelSpars )

data WingModel = WingModel
  { wmName   :: !String        -- ^ human-readable model identifier
  , wmWing   :: !WingShape     -- ^ outline: span, shoulder, stations (with per-station section/structure)
  , wmSpars  :: !SparInventory -- ^ carbon-rod inventory driving spanwise EI/GJ
  , wmMassKg :: !Double        -- ^ dry mass of the whole bird [kg] (Chaos body mass + airframe inertia)
  } deriving stock (Eq, Show)

-- | The reference kestrel: 0.55 m half-span (1.10 m full span), 50 g dry mass,
-- 2.0 mm inner → 1.6 mm outer LE mainspar + diagonal/mid-chord brace box.
-- PTK 7465 (or 7465 Plus) servo.
kestrelModel :: WingModel
kestrelModel = WingModel "kestrel" defaultBirdWing kestrelSpars 0.050

-- | RavenCrow (nature valley): 0.66 m half-span (1.32 m full span), the kestrel
-- outline scaled 1.2×. Its spars are its OWN list (the kestrel construction at
-- 1.2×, written out with specific diameters/lengths so any rod can be edited
-- independently). Mass scales ~s³ (structural similarity): 50 g × 1.2³.
ravenModel :: WingModel
ravenModel = WingModel "raven" (scaleWing 1.2 defaultBirdWing) ravenSpars 0.086

-- | Prototype (training arena): 0.495 m half-span (0.99 m full span), the
-- kestrel outline scaled 0.9×, with its own 0.9× spar list. Mass 50 g × 0.9³.
prototypeModel :: WingModel
prototypeModel = WingModel "prototype" (scaleWing 0.9 defaultBirdWing) prototypeSpars 0.036

-- | Neon turtle (void level): reuses the kestrel outline; the void disables
-- gravity and uses dense-medium drag, so its mass is only a placeholder.
turtleModel :: WingModel
turtleModel = WingModel "turtle" defaultBirdWing kestrelSpars 0.050

-- | Light kestrel sub-variant: the SAME outline as the reference kestrel but
-- half the dry mass (25 g) and every carbon rod thinned by @0.5^(1/4) ≈ 0.841@.
-- Because bending/torsional stiffness scales as the rod diameter to the 4th
-- power (EI, GJ ∝ d⁴), thinning the rods by 0.841 exactly halves the stiffness,
-- which cancels the half weight — so this wing flexes to the SAME ideal airfoil
-- as the 50 g kestrel. It pairs with a weaker, faster servo (Blue Arrow AF
-- D43S-6.0-MG), whose higher no-load speed earns a faster flap at the lighter
-- bird's natural cadence.
kestrelLightModel :: WingModel
kestrelLightModel = WingModel "kestrel-light" defaultBirdWing kestrelLightSpars 0.025

-- | Rod-diameter scale that halves stiffness: EI, GJ ∝ d⁴ ⇒ half stiffness at
-- Ø × 0.5^(1/4).
kestrelLightRodScale :: Double
kestrelLightRodScale = (1/2) ** (1/4)

-- | The light kestrel's spar inventory: the kestrel construction with every rod
-- thinned by 'kestrelLightRodScale'. Lengths, lever arms, the tensioned-membrane
-- baseline and the shared bend/twist calibration gains are unchanged — the wing
-- is the same size, just lighter and softer in exact proportion to its weight.
kestrelLightSpars :: SparInventory
kestrelLightSpars = kestrelSpars
  { siSpars = map thin (siSpars kestrelSpars) }
  where
    thin s = s { spDiameter = spDiameter s * kestrelLightRodScale }

-- | The splice fraction where the inner main spar hands off to the outer main
-- spar. It is the ratio inner-length ∕ half-span; because uniform scaling
-- multiplies both, it is invariant across the kestrel / raven / prototype.
spliceFrac :: Double
spliceFrac = 222e-3 / 0.55  -- ≈ 0.4036

-- | Raven spar inventory: kestrel material + calibration, a 1.2×³ tensioned-
-- membrane baseline, and the raven's own explicit spar list.
ravenSpars :: SparInventory
ravenSpars = kestrelSpars
  { siMembraneGJ = siMembraneGJ kestrelSpars * 1.2 ^ 3
  , siSpars = ravenSparList
  }

-- | Prototype spar inventory: 0.9×³ membrane baseline + its own spar list.
prototypeSpars :: SparInventory
prototypeSpars = kestrelSpars
  { siMembraneGJ = siMembraneGJ kestrelSpars * 0.9 ^ 3
  , siSpars = prototypeSparList
  }

-- | Raven's individual spars (1.2× the kestrel construction).
ravenSparList :: [Spar]
ravenSparList =
  [ Spar "inner main" SparMain  2.40e-3 266.4e-3 0.0 0.0      spliceFrac
  , Spar "outer main" SparMain  1.92e-3 372.0e-3 0.0 spliceFrac 1.0
  , Spar "diagonal"   SparBrace 0.96e-3 255.6e-3 253.2e-3 0.0 spliceFrac
  , Spar "mid chord"  SparBrace 0.96e-3 212.4e-3 205.2e-3 0.0 spliceFrac
  ]

-- | Prototype's individual spars (0.9× the kestrel construction).
prototypeSparList :: [Spar]
prototypeSparList =
  [ Spar "inner main" SparMain  1.80e-3 199.8e-3 0.0 0.0      spliceFrac
  , Spar "outer main" SparMain  1.44e-3 279.0e-3 0.0 spliceFrac 1.0
  , Spar "diagonal"   SparBrace 0.72e-3 191.7e-3 189.9e-3 0.0 spliceFrac
  , Spar "mid chord"  SparBrace 0.72e-3 159.3e-3 153.9e-3 0.0 spliceFrac
  ]