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
-- The spar geometry + material are carried in a 'SparInventory' record — the
-- per-model \"carbon rods\" configuration. The solver derives every stiffness
-- from that record, so a different bird (thicker rods, other brace layout) is a
-- new 'SparInventory' value, never a new code path.
module Born2Flap.Math.Structure
  ( StructureProfile(..)
  , SparInventory(..)
  , kestrelSpars
  , scaleSpars
  , armStructure
  , handStructure
    -- * Cantilever beam integration
  , integrateFlapBeam
  , integrateTwist
    -- * Relaxation
  , relaxDeflection
  , softSaturate
    -- * Spanwise spar stiffness (kestrel construction)
  , sparBendEISpanwise
  , sparTwistGJSpanwise
  ) where

-- | Per-station structural material of one spanwise station.
data StructureProfile = StructureProfile
  { stBendEI   :: !Double  -- ^ flap bending stiffness @EI@ [N·m²]
  , stTwistGJ  :: !Double  -- ^ torsional stiffness @GJ@ [N·m²]
  , stTauBend  :: !Double  -- ^ bending relaxation time constant [s]
  , stTauTwist :: !Double  -- ^ torsion relaxation time constant [s]
  } deriving stock (Eq, Show)

-- | Physical spar inventory of one membrane wing — the model-specific \"carbon
-- rods\" configuration. Everything the beam integrator needs to know about the
-- wing's structure lives here; nothing structural is a bare constant elsewhere.
--
-- The kestrel inventory (kestrelwing.svg), scaled 1.27× to the 1.10 m bird with
-- the two main spars upgraded for the bigger servo:
--
--   inner base mainspar  Ø 2.0 mm  — inboard LE, full arm wing (to the elbow)
--   outer hand mainspar  Ø 1.6 mm  — outboard LE, hand wing only
--   diagonal spar        Ø 0.8 mm  — LE→TE brace (twist), inner wing only
--   mid chord spar       Ø 0.6 mm  — chordwise brace (extend + twist), inner wing
--
-- Material: pultruded carbon rod, E = 135 GPa (bending), G = 5 GPa (shear).
-- Solid round section:  I = π·d⁴/64  (bending),  J = π·d⁴/32  (torsion).
--
-- A membrane wing is far stiffer out of plane than a bare rod: the tensioned
-- membrane acts as a stressed skin and the diagonal brace trusses the leading
-- edge, so *bending* is stiff and the spar flexes only slightly. *Torsion* is
-- the opposite — the only resistance is the braces' own bending, so the hand
-- wing washes out freely under the nose-down pitching moment. The two are
-- therefore calibrated independently (bend scale vs twist scale).
data SparInventory = SparInventory
  { siYoungsModulus :: !Double  -- ^ carbon rod Young's modulus [Pa] (bending)
  , siShearModulus  :: !Double  -- ^ carbon rod shear modulus [Pa] (torsion)
  , siBendScale     :: !Double  -- ^ out-of-plane bending calibration (stressed-skin/truss)
  , siTwistScale    :: !Double  -- ^ torsional calibration
  , siDInnerMain    :: !Double  -- ^ inner main-spar diameter [m]
  , siDOuterMain    :: !Double  -- ^ outer hand main-spar diameter [m]
  , siDDiagonal     :: !Double  -- ^ diagonal LE→TE brace diameter [m]
  , siDMidChord     :: !Double  -- ^ mid-chord brace diameter [m]
  , siInnerLengthM  :: !Double  -- ^ inner main-spar length before the splice [m]
  , siDiagLengthM   :: !Double  -- ^ diagonal brace path length [m]
  , siDiagLeverM    :: !Double  -- ^ chordwise offset the diagonal brace acts through [m]
  , siMidLengthM    :: !Double  -- ^ mid-chord brace path length [m]
  , siMidLeverM     :: !Double  -- ^ chordwise offset the mid-chord brace acts through [m]
  , siMembraneGJ    :: !Double  -- ^ tensioned-membrane baseline torsional stiffness [N·m²]
  , siBraceScale    :: !Double  -- ^ braced arm-wing torsional calibration
  } deriving stock (Eq, Show)

-- | The kestrel carbon-rod inventory (see 'SparInventory' docs for the layout).
kestrelSpars :: SparInventory
kestrelSpars = SparInventory
  { siYoungsModulus = 135e9
  , siShearModulus  = 5e9
  , siBendScale     = 60.0
  , siTwistScale    = 1.0
  , siDInnerMain    = 2.0e-3
  , siDOuterMain    = 1.6e-3
  , siDDiagonal     = 0.80e-3
  , siDMidChord     = 0.60e-3
  , siInnerLengthM  = 222e-3
  , siDiagLengthM   = 213e-3
  , siDiagLeverM    = 211e-3
  , siMidLengthM    = 177e-3
  , siMidLeverM     = 171e-3
  , siMembraneGJ    = 2.2e-1
  , siBraceScale    = 4.0
  }

-- | Scale a spar inventory by a geometric factor @s@ (structural similarity):
-- every rod diameter and brace length scales by @s@, the tensioned-membrane
-- baseline torsional stiffness scales by @s³@, and the dimensionless
-- calibration gains (bend/twist/brace scale) are unchanged. Used to derive a
-- second bird's carbon rods from the kestrel inventory without introducing
-- new magic constants — a bigger bird gets proportionally longer, thicker rods.
scaleSpars :: Double -> SparInventory -> SparInventory
scaleSpars s inv = inv
  { siDInnerMain   = siDInnerMain inv * s
  , siDOuterMain   = siDOuterMain inv * s
  , siDDiagonal    = siDDiagonal inv * s
  , siDMidChord    = siDMidChord inv * s
  , siInnerLengthM = siInnerLengthM inv * s
  , siDiagLengthM  = siDiagLengthM inv * s
  , siDiagLeverM   = siDiagLeverM inv * s
  , siMidLengthM   = siMidLengthM inv * s
  , siMidLeverM    = siMidLeverM inv * s
  , siMembraneGJ   = siMembraneGJ inv * s * s * s
  }

-- | Bending stiffness @EI@ [N·m²] of a solid round carbon rod of diameter @d@.
sparBendEI :: SparInventory -> Double -> Double
sparBendEI inv d = siYoungsModulus inv * pi * d ^ 4 / 64

-- | Torsional stiffness @GJ@ [N·m²] of a solid round carbon rod of diameter @d@.
sparTorsionGJ :: SparInventory -> Double -> Double
sparTorsionGJ inv d = siShearModulus inv * pi * d ^ 4 / 32

-- | Leading-edge main-spar diameter at a span fraction, given the half-span.
sparMainDiameter :: SparInventory -> Double -> Double -> Double
sparMainDiameter inv spanM frac
  | frac < sparInnerFraction inv spanM = siDInnerMain inv
  | otherwise = siDOuterMain inv

-- | Span fraction where the inner main spar splices into the outer hand spar.
sparInnerFraction :: SparInventory -> Double -> Double
sparInnerFraction inv spanM = clamp 0 1 (siInnerLengthM inv / spanM)

-- | Spanwise flap-bending stiffness [N·m²]: the LE main spar (inner → outer
-- hand) bends the wing out of plane.
sparBendEISpanwise :: SparInventory -> Double -> Double -> Double
sparBendEISpanwise inv spanM frac = siBendScale inv * sparBendEI inv (sparMainDiameter inv spanM frac)

-- | Diagonal-brace effective torsional stiffness [N·m²]: the brace resists the
-- trailing edge swinging under twist by bending as a cantilever.
sparBraceGJ :: SparInventory -> Double
sparBraceGJ inv = 3 * sparBendEI inv (siDDiagonal inv) / siDiagLengthM inv ^ 3 * siDiagLeverM inv ^ 2

-- | Mid-chord-brace effective torsional stiffness [N·m²]. A chordwise brace
-- keeps the section extended and couples the trailing edge to the diagonal
-- spar; its bending resists the section twisting. Secondary to the diagonal.
sparMidChordBraceGJ :: SparInventory -> Double
sparMidChordBraceGJ inv = 3 * sparBendEI inv (siDMidChord inv) / siMidLengthM inv ^ 3 * siMidLeverM inv ^ 2

-- | Spanwise torsional stiffness [N·m²]: the tensioned membrane (whole wing)
-- plus the diagonal + mid-chord braces (inner wing) resist twist; outboard only
-- the membrane + the thin hand-wing rod remain, so the hand wing washes out
-- under the nose-down pitching moment without folding.
sparTwistGJSpanwise :: SparInventory -> Double -> Double -> Double
sparTwistGJSpanwise inv spanM frac =
  siTwistScale inv *
    if frac < sparInnerFraction inv spanM
      then sparTorsionGJ inv (siDInnerMain inv) + siBraceScale inv * (sparBraceGJ inv + sparMidChordBraceGJ inv) + siMembraneGJ inv
      else sparTorsionGJ inv (siDOuterMain inv) + siMembraneGJ inv

-- | Bird armwing (kestrel inner wing): 2.0 mm main spar + diagonal & mid-chord
-- braces. Stiff in bending, moderately stiff in torsion.
armStructure :: SparInventory -> StructureProfile
armStructure inv = StructureProfile
  { stBendEI = siBendScale inv * sparBendEI inv (siDInnerMain inv)
  , stTwistGJ = siTwistScale inv * (sparTorsionGJ inv (siDInnerMain inv) + siBraceScale inv * (sparBraceGJ inv + sparMidChordBraceGJ inv) + siMembraneGJ inv)
  , stTauBend = 0.03
  , stTauTwist = 0.03
  }

-- | Bird handwing (kestrel outer hand): 1.6 mm main spar, no braces outboard —
-- so it is much softer in torsion, the source of washout-under-load.
handStructure :: SparInventory -> StructureProfile
handStructure inv = StructureProfile
  { stBendEI = siBendScale inv * sparBendEI inv (siDOuterMain inv)
  , stTwistGJ = siTwistScale inv * (sparTorsionGJ inv (siDOuterMain inv) + siMembraneGJ inv)
  , stTauBend = 0.04
  , stTauTwist = 0.04
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
-- plateau with a slope discontinuity -- the visible \"fold\" in the spar line.
-- tanh approaches the limit smoothly, so the spar only ever curves, never kinks,
-- no matter how large the load.
softSaturate :: Double -> Double -> Double
softSaturate limit x = limit * tanh (x / max 1.0e-9 limit)

clamp :: Ord a => a -> a -> a -> a
clamp low high = max low . min high