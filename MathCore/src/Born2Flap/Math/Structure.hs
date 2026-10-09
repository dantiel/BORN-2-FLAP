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
module Born2Flap.Math.Structure
  ( StructureProfile(..)
  , defaultBirdArmStructure
  , defaultBirdHandStructure
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

-- | Physical spar inventory of the kestrel membrane wing (kestrelwing.svg):
--
--   inner base mainspar  Ø 1.6 mm  — inboard LE, first 175 mm of inner wing
--   outer hand mainspar  Ø 1.2 mm  — outboard LE, rest of the span
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
sparYoungsModulus, sparShearModulus :: Double
sparYoungsModulus = 135e9
sparShearModulus = 5e9

-- | Out-of-plane (flap) bending calibration. Stressed-skin + truss action make
-- the wing far stiffer than the bare LE rod. Set to 1 for the raw rod value.
sparBendScale :: Double
sparBendScale = 88.0

-- | Torsional calibration. The braces' bending is the real resistance, already
-- physical (set to 1 for the raw value); raising it over-stiffens the twist.
sparTwistScale :: Double
sparTwistScale = 1.0

sparDInnerMain, sparDOuterMain, sparDDiagonal, sparDMidChord :: Double
sparDInnerMain = 1.6e-3
sparDOuterMain = 1.2e-3
sparDDiagonal  = 0.80e-3
sparDMidChord  = 0.60e-3

-- | Inboard length of the 1.6 mm inner main spar [m], before it splices into the
-- 1.2 mm outer hand spar.
sparInnerLengthM :: Double
sparInnerLengthM = 175e-3

sparBendEI, sparTorsionGJ :: Double -> Double
sparBendEI d = sparYoungsModulus * pi * d ^ 4 / 64
sparTorsionGJ d = sparShearModulus * pi * d ^ 4 / 32

-- | Leading-edge main-spar diameter at a span fraction, given the half-span.
sparMainDiameter :: Double -> Double -> Double
sparMainDiameter spanM frac
  | frac < sparInnerFraction spanM = sparDInnerMain
  | otherwise = sparDOuterMain

-- | Span fraction where the 1.6 mm inner spar splices into the 1.2 mm outer hand.
sparInnerFraction :: Double -> Double
sparInnerFraction spanM = clamp 0 1 (sparInnerLengthM / spanM)

-- | Spanwise flap-bending stiffness [N·m²]: the LE main spar (1.6 mm inner → 1.2 mm
-- outer hand) bends the wing out of plane.
sparBendEISpanwise :: Double -> Double -> Double
sparBendEISpanwise spanM frac = sparBendScale * sparBendEI (sparMainDiameter spanM frac)

-- | Diagonal-brace effective torsional stiffness [N·m²]: the brace resists the
-- trailing edge swinging under twist by bending as a cantilever.
sparBraceGJ :: Double
sparBraceGJ = 3 * sparBendEI sparDDiagonal / sparDiagLength ^ 3 * sparChordLever ^ 2
  where
    sparDiagLength = 168e-3   -- [m] path length of the "diagonal spar"
    sparChordLever = 166e-3   -- [m] chordwise offset of the brace

-- | Mid-chord-brace effective torsional stiffness [N·m²]. A chordwise brace
-- keeps the section extended and couples the trailing edge to the diagonal
-- spar; its bending resists the section twisting. Secondary to the diagonal.
sparMidChordBraceGJ :: Double
sparMidChordBraceGJ = 3 * sparBendEI sparDMidChord / sparMidLength ^ 3 * sparMidLever ^ 2
  where
    sparMidLength = 139e-3   -- [m] chordwise path length (mid chord spar)
    sparMidLever  = 135e-3   -- [m] chordwise offset it acts through

-- | Tensioned-membrane torsional stiffness [N·m²]: the membrane spans the chord
-- everywhere and resists the trailing edge swinging under twist. This is the
-- baseline torsional resistance across the whole wing; the braces add to it
-- inboard. Without it the bare hand-wing rod is ~100× softer than the armwing
-- and the outboard washout saturates into a spurious nose-up fold.
sparMembraneGJ :: Double
sparMembraneGJ = 8.0e-2

-- | Braced arm-wing torsional calibration. The diagonal + mid-chord braces form
-- a triangulated spar box whose torsional resistance is several times the bare
-- cantilever estimate (3·EI/L³·lever²). This scale lifts the arm wing's GJ so it
-- stays torsionally stiff against the accumulated outboard torque — only the
-- unbraced hand wing washes out under load, the inner wing keeps its incidence.
sparBraceScale :: Double
sparBraceScale = 4.0

-- | Spanwise torsional stiffness [N·m²]: the tensioned membrane (whole wing)
-- plus the diagonal + mid-chord braces (inner wing) resist twist; outboard only
-- the membrane + the thin hand-wing rod remain, so the hand wing washes out
-- under the nose-down pitching moment without folding.
sparTwistGJSpanwise :: Double -> Double -> Double
sparTwistGJSpanwise spanM frac =
  sparTwistScale *
    if frac < sparInnerFraction spanM
      then sparTorsionGJ sparDInnerMain + sparBraceScale * (sparBraceGJ + sparMidChordBraceGJ) + sparMembraneGJ
      else sparTorsionGJ sparDOuterMain + sparMembraneGJ

-- | Bird armwing (kestrel inner wing): 1.6 mm main spar + diagonal & mid-chord
-- braces. Stiff in bending, moderately stiff in torsion.
defaultBirdArmStructure :: StructureProfile
defaultBirdArmStructure = StructureProfile
  { stBendEI = sparBendScale * sparBendEI sparDInnerMain
  , stTwistGJ = sparTwistScale * (sparTorsionGJ sparDInnerMain + sparBraceScale * (sparBraceGJ + sparMidChordBraceGJ) + sparMembraneGJ)
  , stTauBend = 0.03
  , stTauTwist = 0.03
  }

-- | Bird handwing (kestrel outer hand): 1.2 mm main spar, no braces outboard —
-- so it is much softer in torsion, the source of washout-under-load.
defaultBirdHandStructure :: StructureProfile
defaultBirdHandStructure = StructureProfile
  { stBendEI = sparBendScale * sparBendEI sparDOuterMain
  , stTwistGJ = sparTwistScale * (sparTorsionGJ sparDOuterMain + sparMembraneGJ)
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
-- plateau with a slope discontinuity -- the visible "fold" in the spar line.
-- tanh approaches the limit smoothly, so the spar only ever curves, never kinks,
-- no matter how large the load.
softSaturate :: Double -> Double -> Double
softSaturate limit x = limit * tanh (x / max 1.0e-9 limit)

clamp :: Ord a => a -> a -> a -> a
clamp low high = max low . min high