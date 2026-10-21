{-# LANGUAGE DerivingStrategies #-}
{-# LANGUAGE StrictData #-}

-- | Spanwise structural properties + cantilever-beam integration for 3D
-- aeroelastic bending.
--
-- This is the *structural* complement of the @Section@ membrane model. Where
-- @SectionProfile@ describes how a section cups deeper under chordwise load,
-- @StructureProfile@ describes how the whole wing bends and twists out of
-- plane under the aerodynamic loads. Together they turn the rigid outline into
-- a flexible wing whose shape in 3D is a *consequence* of its design and its
-- current loading — a bird, butterfly, dragonfly or pterosaur only differ by
-- these per-station numbers.
--
-- The model is a root-clamped Euler–Bernoulli cantilever, integrated
-- discretely along the span:
--
--   * __flap bending__ (out of plane) — vertical force per strip accumulates
--     into a bending moment, moment ∕ stiffness gives curvature, curvature is
--     integrated twice from the clamped root into deflection and slope.
--
--   * __torsion__ — the outboard sectional pitching moment (camber/reflex
--     nose-down moment) accumulates into a torque, torque ∕ torsional
--     stiffness gives twist rate, integrated once from the clamped root.
--
-- Both are quasi-static targets relaxed into the strip state with a first-order
-- lag, so the aeroelastic feedback loop is stable at fixed timestep.
--
-- The structure is carried as an OPEN-ENDED LIST of 'Spar' records inside the
-- 'SparInventory' — the per-model "carbon rods" configuration. Each spar has
-- its own diameter (thickness), physical length, chordwise lever arm and
-- spanwise extent, so a wing can have any number of spars and every bird can
-- have a genuinely individual structure. The solver derives every stiffness
-- from that list, never from a hard-coded constant.
module Born2Flap.Math.Structure
  ( StructureProfile(..)
  , SparKind(..)
  , Spar(..)
  , SparInventory(..)
  , kestrelSpars
  , kestrelSparList
  , scaleSpars
  , armStructure
  , handStructure
    -- * Cantilever beam integration
  , integrateFlapBeam
  , integrateTwist
    -- * Relaxation
  , relaxDeflection
  , softSaturate
    -- * Spanwise spar stiffness
  , sparBendEI
  , sparTorsionGJ
  , sparBraceGJ
  , sparBendEISpanwise
  , sparTwistGJSpanwise
  ) where

import Data.Maybe (listToMaybe)

-- | Per-station structural material of one spanwise station.
data StructureProfile = StructureProfile
  { stBendEI   :: !Double  -- ^ flap bending stiffness @EI@ [N·m²]
  , stTwistGJ  :: !Double  -- ^ torsional stiffness @GJ@ [N·m²]
  , stTauBend  :: !Double  -- ^ bending relaxation time constant [s]
  , stTauTwist :: !Double  -- ^ torsion relaxation time constant [s]
  } deriving stock (Eq, Show)

-- | Structural role of a spar: how its stiffness is computed and what it
-- resists.
data SparKind
  = SparMain   -- ^ longitudinal (leading-edge) spar: carries flap bending EI and its own torsion GJ
  | SparBrace  -- ^ chordwise/diagonal brace: its cantilever bending resists section twist
  deriving stock (Eq, Show)

-- | A single spar (carbon rod) of the wing. A wing is an *arbitrary list* of
-- these, each with its own diameter (thickness), physical length, chordwise
-- lever arm and spanwise extent. This is the "individual wing" primitive: add
-- a spar to the list, or give a bird its own list, and the beam integrator
-- follows — no solver code changes and no magic constants.
data Spar = Spar
  { spName     :: !String  -- ^ identifier (e.g. @"inner main"@, @"mid chord"@)
  , spKind     :: !SparKind
  , spDiameter :: !Double  -- ^ rod diameter (thickness) [m]
  , spLengthM  :: !Double  -- ^ physical rod length [m]
  , spLeverM   :: !Double  -- ^ chordwise moment arm [m] (braces only; 0 for main spars)
  , spRootFrac :: !Double  -- ^ span fraction [0..1] where this spar begins carrying load
  , spTipFrac  :: !Double  -- ^ span fraction [0..1] where this spar stops (half-open)
  } deriving stock (Eq, Show)

-- | Physical spar inventory of one membrane wing — the model-specific "carbon
-- rods" configuration, now an open-ended list. The solver derives every
-- stiffness from the list, so a different bird is a different list, never a new
-- code path.
data SparInventory = SparInventory
  { siYoungsModulus :: !Double  -- ^ carbon rod Young's modulus [Pa] (bending)
  , siShearModulus  :: !Double  -- ^ carbon rod shear modulus [Pa] (torsion)
  , siBendScale     :: !Double  -- ^ out-of-plane bending calibration (stressed-skin/truss)
  , siTwistScale    :: !Double  -- ^ torsional calibration
  , siMembraneGJ    :: !Double  -- ^ tensioned-membrane baseline torsional stiffness [N·m²]
  , siBraceScale    :: !Double  -- ^ braced arm-wing torsional calibration
  , siSpars         :: ![Spar]  -- ^ the spars (main + braces), any number
  } deriving stock (Eq, Show)

-- | The kestrel carbon-rod inventory. Material: pultruded carbon rod,
-- E = 135 GPa (bending), G = 5 GPa (shear). Solid round section:
-- I = π·d⁴/64, J = π·d⁴/32.
--
-- Layout (kestrelwing.svg), on a 0.55 m half-span:
--
--   inner main spar  Ø 2.0 mm, 222 mm — root → elbow (inboard leading edge)
--   outer main spar  Ø 1.6 mm, 310 mm — elbow → tip  (outboard leading edge)
--   diagonal brace   Ø 0.8 mm, 213 mm — LE→TE truss, resists twist (inner wing)
--   mid-chord brace  Ø 0.8 mm, 177 mm — chordwise brace: keeps the section
--                                          open/flat, resists twist (inner wing)
--
-- A membrane wing is far stiffer out of plane than a bare rod: the tensioned
-- membrane acts as a stressed skin and the diagonal brace trusses the leading
-- edge, so *bending* is stiff and the spar flexes only slightly. *Torsion* is
-- the opposite — the only resistance is the braces' own bending, so the hand
-- wing washes out freely under the nose-down pitching moment. The two are
-- therefore calibrated independently (bend scale vs twist scale).
kestrelSpars :: SparInventory
kestrelSpars = SparInventory
  { siYoungsModulus = 135e9
  , siShearModulus  = 5e9
    -- Calibrated for the 50 g reference kestrel: 2× stiffer than the prior 25 g
    -- calibration (siBendScale 130 → 260), so the full 0.49 N design load flexes
    -- the wing to the SAME ideal airfoil the 25 g bird reached at 130 — keeping
    -- the aeroelastic response in its resonant range at 50 g. (kestrelLightSpars
    -- thins the rods by 0.5^(1/4) so the 25 g variant reaches that same flex.)
    , siBendScale     = 260.0
    , siTwistScale    = 4.0
    , siMembraneGJ    = 1.0e-2
    , siBraceScale    = 0.4
  , siSpars         = kestrelSparList
  }

-- | The kestrel's individual spar list. The inner/outer main spars splice at
-- the fraction where the inner spar's 222 mm length ends on the 0.55 m
-- half-span; the two braces only brace the inner wing up to that splice.
kestrelSparList :: [Spar]
kestrelSparList =
  let spliceFrac = 222e-3 / 0.55  -- ≈ 0.4036
  in [ Spar "inner main" SparMain  2.0e-3 222e-3 0.0 0.0      spliceFrac
     , Spar "outer main" SparMain  1.6e-3 310e-3 0.0 spliceFrac 1.0
     , Spar "diagonal"   SparBrace 0.8e-3 213e-3 211e-3 0.0 spliceFrac
     , Spar "mid chord"  SparBrace 0.8e-3 177e-3 171e-3 0.0 spliceFrac
     ]

-- | Scale a spar inventory by a geometric factor @s@ (structural similarity):
-- every rod diameter, length and lever arm scale by @s@, the tensioned-membrane
-- baseline torsional stiffness scales by @s³@, and the dimensionless
-- calibration gains (bend/twist/brace) are unchanged. Spanwise extents are
-- fractions, so they are invariant under scaling. Use this to derive a
-- geometrically-similar bird; for a truly individual wing, build the 'Spar'
-- list directly (see 'kestrelSparList').
scaleSpars :: Double -> SparInventory -> SparInventory
scaleSpars s inv = inv
  { siSpars      = map scaleOne (siSpars inv)
  , siMembraneGJ = siMembraneGJ inv * s * s * s
  }
  where
    scaleOne sp = sp
      { spDiameter = spDiameter sp * s
      , spLengthM  = spLengthM sp * s
      , spLeverM   = spLeverM sp * s
      }

-- | Bending stiffness @EI@ [N·m²] of a solid round carbon rod of diameter @d@.
sparBendEI :: SparInventory -> Double -> Double
sparBendEI inv d = siYoungsModulus inv * pi * d ^ 4 / 64

-- | Torsional stiffness @GJ@ [N·m²] of a solid round carbon rod of diameter @d@.
sparTorsionGJ :: SparInventory -> Double -> Double
sparTorsionGJ inv d = siShearModulus inv * pi * d ^ 4 / 32

-- | Is a spar carrying load at this span fraction? Half-open @[root, tip)@, so
-- spliced main spars never double-count at their shared boundary.
sparActiveAt :: Spar -> Double -> Bool
sparActiveAt s frac = frac >= spRootFrac s && frac < spTipFrac s

-- | Main (leading-edge) spars only.
mainSpars :: SparInventory -> [Spar]
mainSpars inv = [ s | s <- siSpars inv, spKind s == SparMain ]

-- | Brace (chordwise/diagonal) spars only.
braceSpars :: SparInventory -> [Spar]
braceSpars inv = [ s | s <- siSpars inv, spKind s == SparBrace ]

-- | Diameter of the root-anchored (inboard) main spar, if any.
inboardMainDiameter :: SparInventory -> Double
inboardMainDiameter inv =
  maybe 0 spDiameter (listToMaybe [ s | s <- mainSpars inv, spRootFrac s <= 0 ])

-- | Diameter of the tip-reaching (outboard) main spar, if any.
outboardMainDiameter :: SparInventory -> Double
outboardMainDiameter inv =
  maybe 0 spDiameter (listToMaybe [ s | s <- mainSpars inv, spTipFrac s >= 1 ])

-- | Spanwise flap-bending stiffness [N·m²]: the bend calibration times the sum
-- of every active main spar's EI. Braces do not contribute to flap bending.
sparBendEISpanwise :: SparInventory -> Double -> Double
sparBendEISpanwise inv frac =
  siBendScale inv * sum [ sparBendEI inv (spDiameter s) | s <- mainSpars inv, sparActiveAt s frac ]

-- | A brace's effective torsional stiffness [N·m²]: it resists the trailing
-- edge swinging under twist by bending as a cantilever.
sparBraceGJ :: SparInventory -> Spar -> Double
sparBraceGJ inv s = 3 * sparBendEI inv (spDiameter s) / (spLengthM s ^ 3) * (spLeverM s ^ 2)

-- | Spanwise torsional stiffness [N·m²]: the tensioned membrane (whole wing),
-- plus the active main spars' own torsion and the active braces' bending
-- resistance. Outboard only the membrane + the thin hand-wing rod remain, so
-- the hand wing washes out under the nose-down pitching moment without folding.
sparTwistGJSpanwise :: SparInventory -> Double -> Double
sparTwistGJSpanwise inv frac =
  siTwistScale inv *
    ( siMembraneGJ inv
    + sum [ sparTorsionGJ inv (spDiameter s) | s <- mainSpars inv, sparActiveAt s frac ]
    + siBraceScale inv * sum [ sparBraceGJ inv s | s <- braceSpars inv, sparActiveAt s frac ]
    )

-- | Bird armwing (inboard inner wing): root-anchored main spar + the braces.
-- Stiff in bending, moderately stiff in torsion (the diagonal + mid-chord
-- braces resist the section twisting).
armStructure :: SparInventory -> StructureProfile
armStructure inv = StructureProfile
  { stBendEI = siBendScale inv * sparBendEI inv (inboardMainDiameter inv)
  , stTwistGJ = siTwistScale inv *
      ( sparTorsionGJ inv (inboardMainDiameter inv)
      + siBraceScale inv * sum [ sparBraceGJ inv s | s <- braceSpars inv ]
      + siMembraneGJ inv )
  , stTauBend = 0.05
  , stTauTwist = 0.05
  }

-- | Bird handwing (outboard hand wing): tip-reaching main spar, no braces —
-- so it is much softer in torsion, the source of washout-under-load.
handStructure :: SparInventory -> StructureProfile
handStructure inv = StructureProfile
  { stBendEI = siBendScale inv * sparBendEI inv (outboardMainDiameter inv)
  , stTwistGJ = siTwistScale inv * (sparTorsionGJ inv (outboardMainDiameter inv) + siMembraneGJ inv)
  , stTauBend = 0.07
  , stTauTwist = 0.07
  }

-- | Cantilever (clamped root) out-of-plane bending under a distributed load.
-- Given per-station vertical forces @fz@ (root → tip, [N]) and bending
-- stiffnesses @EI@, returns @(deflections, slopes)@ — both root-clamped, with
-- the tip free. A uniform upward load therefore bends the tip upward and the
-- slope increases monotonically toward the tip.
integrateFlapBeam :: [Double] -> [Double] -> Double -> ([Double], [Double])
integrateFlapBeam forces stiffnesses dr
  | length forces < 2 || length forces /= length stiffnesses =
      (replicate (length forces) 0, replicate (length forces) 0)
  | otherwise =
      let n = length forces
          -- Bending moment at station i = sum of all outboard forces times
          -- their lever arm about station i (station spacing dr).
          momentAt i = sum
            [ (forces !! j) * (fromIntegral (j - i) * dr)
            | j <- [i .. n - 1] ]
          curvatures =
            [ momentAt i / max 1.0e-6 (stiffnesses !! i) | i <- [0 .. n - 1] ]
          -- Integrate curvature → slope, then slope → deflection.
          slopes = scanl (\slope kappa -> slope + kappa * dr) 0 curvatures
          deflections = scanl (\w slope -> w + slope * dr) 0 (take n slopes)
      in (take n deflections, take n slopes)

-- | Cantilever torsion under distributed sectional pitching moments. Given
-- per-station torques (root → tip, [N·m]) and torsional stiffnesses @GJ@,
-- returns the twist per station [rad], root-clamped. A nose-down (negative)
-- outboard moment therefore washes the tip out (negative twist).
integrateTwist :: [Double] -> [Double] -> Double -> [Double]
integrateTwist torques stiffnesses dr
  | length torques < 2 || length torques /= length stiffnesses =
      replicate (length torques) 0
  | otherwise =
      let n = length torques
          torqueAt i = sum [ torques !! j | j <- [i .. n - 1] ]
          twistRates =
            [ torqueAt i / max 1.0e-6 (stiffnesses !! i) | i <- [0 .. n - 1] ]
          twists = scanl (\phi rate -> phi + rate * dr) 0 twistRates
      in take n twists

-- | First-order relaxation of a deflection toward a target (unconditionally
-- stable: for @dt <= 0@ holds, for @tau <= 0@ snaps to target).
relaxDeflection :: Double -> Double -> Double -> Double -> Double
relaxDeflection dt tau current target
  | dt <= 0 = current
  | tau <= 0 = target
  | otherwise = current + (target - current) * (1 - exp (-dt / tau))

-- | Soft asymptotic saturation, replacing the hard deflection clamp. A hard
-- clamp flattens every outboard station whose target exceeds the limit into a
-- plateau with a slope discontinuity -- the visible "fold" in the spar line.
-- tanh approaches the limit smoothly, so the spar only ever curves, never kinks,
-- no matter how large the load.
softSaturate :: Double -> Double -> Double
softSaturate limit x = limit * tanh (x / max 1.0e-9 limit)