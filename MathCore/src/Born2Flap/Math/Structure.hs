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
  ) where

-- | Per-station structural material of one spanwise station.
data StructureProfile = StructureProfile
  { stBendEI   :: !Double  -- ^ flap bending stiffness @EI@ [N·m²]
  , stTwistGJ  :: !Double  -- ^ torsional stiffness @GJ@ [N·m²]
  , stTauBend  :: !Double  -- ^ bending relaxation time constant [s]
  , stTauTwist :: !Double  -- ^ torsion relaxation time constant [s]
  } deriving stock (Eq, Show)

-- | Bird armwing: a stiffer, feathered panel that resists bending and twisting.
defaultBirdArmStructure :: StructureProfile
defaultBirdArmStructure = StructureProfile
  { stBendEI = 4.0
  , stTwistGJ = 1.5
  , stTauBend = 0.03
  , stTauTwist = 0.03
  }

-- | Bird handwing: thinner primaries, more compliant — bends and twists more,
-- which is what gives a real wing its washout-under-load.
defaultBirdHandStructure :: StructureProfile
defaultBirdHandStructure = StructureProfile
  { stBendEI = 1.2
  , stTwistGJ = 0.5
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
