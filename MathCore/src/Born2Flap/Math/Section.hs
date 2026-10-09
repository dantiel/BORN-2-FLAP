{-# LANGUAGE DerivingStrategies #-}
{-# LANGUAGE StrictData #-}

-- | Passive-adaptive airfoil sections — the material/design properties that turn
-- a rigid outline into an aeroelastic wing, plus the thin-airfoil response they
-- produce.
--
-- A @SectionProfile@ is attached to every spanwise station of the planform (and,
-- later, to every patch of the strip × patch mesh). It expresses exactly the two
-- behaviours the owner asked for:
--
--   * __membrane cupping__ — regions with a nonzero @spMembraneK@ billow *deeper*
--     under load: their camber grows with the local lift coefficient, and
--     reverses when the load reverses (a membrane always bulges toward the side
--     being pushed). @spMembraneTau@ is how fast it settles.
--
--   * __inbuilt reflex__ — regions with @spReflex@ carry a trailing-edge reflex
--     that trims the nose-down pitching moment of their camber toward neutral
--     (0 = conventional cambered section, 1 = flying-wing neutral). This is what
--     couples the wing to the tail: a more reflexed wing needs less tail
--     downforce to hold trim.
--
-- All quantities are dimensionless except @spMembraneTau@ [s]. Camber is carried
-- as a fraction of chord @f/c@ (positive = cambered up). The thin-airfoil
-- approximations below are the robust defaults; they are the tuning surface for
-- later calibration, not hand-fitted magic.
module Born2Flap.Math.Section
  ( SectionProfile(..)
  , defaultBirdArmSection
  , defaultBirdHandSection
    -- * Thin-airfoil response (camber + reflex)
  , zeroLiftAngle
  , sectionPitchMomentCoeff
  , sectionTwistLever
  , sectionTwistMomentCoeff
    -- * Aeroelastic membrane dynamics
  , membraneCamberTarget
  , relaxCamber
  ) where

-- | Per-station material/design properties of one airfoil section.
data SectionProfile = SectionProfile
  { spRestCamber  :: !Double  -- ^ built-in camber @f/c@ (0 = flat, + = cambered up)
  , spReflex      :: !Double  -- ^ reflex 0..1 (0 = none, 1 = neutral pitching moment)
  , spMembraneK   :: !Double  -- ^ membrane compliance: extra camber per unit Cl (billow)
  , spMembraneTau :: !Double  -- ^ membrane relaxation time constant [s]
  } deriving stock (Eq, Show)

-- | Bird armwing: moderate camber, mild reflex, a stiffer feathered surface and
-- only modest membrane billow.
defaultBirdArmSection :: SectionProfile
defaultBirdArmSection = SectionProfile
  { spRestCamber = 0.035
  , spReflex = 0.15
  , spMembraneK = 0.012
  , spMembraneTau = 0.05
  }

-- | Bird handwing: a little less camber, more reflex (washed-out tip feathers
-- that resist the nose-down moment), and a thinner, more membrane-like primary
-- region that cups more readily.
defaultBirdHandSection :: SectionProfile
defaultBirdHandSection = SectionProfile
  { spRestCamber = 0.025
  , spReflex = 0.40
  , spMembraneK = 0.035
  , spMembraneTau = 0.04
  }

-- | Zero-lift angle shift from camber [rad]. Thin-airfoil theory: a cambered
-- mean line with max camber @f/c@ has @alpha₀ ≈ -2·(f/c)@ — positive camber
-- produces lift at zero incidence. This is the @camber → lift@ coupling.
zeroLiftAngle :: Double -> Double
zeroLiftAngle camber = -2.0 * camber

-- | Section pitching-moment coefficient about the quarter-chord [dimensionless],
-- from camber and reflex. Conventional camber is nose-down (negative); reflex
-- (0..1) trims that toward neutral. @momentK = π/2@ is the circular-arc mean-line
-- value — enough to give a real, tunable pitch-trim coupling.
sectionPitchMomentCoeff :: Double -> Double -> Double
sectionPitchMomentCoeff camber reflex =
  let momentK = pi / 2
      trim = clamp01 reflex
  in -momentK * camber * (1.0 - trim)

-- | Chordwise offset of the aerodynamic centre from the *effective* torsional
-- axis, as a fraction of chord, at a given span fraction. Thin-airfoil AC sits
-- at @0.25c@, but the elastic axis is NOT at the leading edge everywhere: the
-- braced inboard arm wing (diagonal LE→TE brace + mid-chord spar) holds its
-- torsional centre AFT (≈0.15c), leaving only ~0.05c for the lift to twist it;
-- the unbraced outboard hand wing has its elastic axis at the leading edge, so
-- the full ~0.20c AC offset twists it. This concentrates aeroelastic washout in
-- the hand wing (the part a real bird twists) and keeps the stiff, braced arm
-- wing from washing out under the accumulated outboard torque — the inner wing
-- stays near its geometric incidence while the tip washes out with load. Using a
-- single full-LE lever (0.25c) everywhere had eaten the lift-curve slope and made
-- the elevator feel weak under load; the spanwise split keeps that authority.
sectionTwistLever :: Double -> Double
sectionTwistLever frac =
  let f = clamp01 frac
      t = clamp01 ((f - 0.15) / 0.45)
      s = t * t * (3.0 - 2.0 * t)   -- smoothstep inboard → outboard
  in 0.05 + 0.15 * s

-- | Section pitching-moment coefficient about the LEADING-EDGE elastic axis
-- (the twist axis), not the quarter-chord AC. It is the camber/reflex AC moment
-- plus the lift acting at the @lever@ offset. The lift term is load-proportional:
-- positive @cl@ deepens the nose-down washout (delaying tip stall), negative @cl@
-- (upstroke / reversed flow) reverses it into nose-up wash-in — the hand-wing
-- reversal a real bird shows on the upstroke.
sectionTwistMomentCoeff :: Double -> Double -> Double -> Double -> Double
sectionTwistMomentCoeff camber reflex lever cl =
  sectionPitchMomentCoeff camber reflex - lever * cl

-- | Membrane cupping target camber @f/c@ under a local lift coefficient @cl@.
-- A positive @cl@ (lift up, suction on top) cups the membrane deeper (more
-- positive camber); a negative @cl@ reverses the cup. @cl@ is signed.
membraneCamberTarget :: SectionProfile -> Double -> Double
membraneCamberTarget profile cl =
  spRestCamber profile + spMembraneK profile * cl

-- | First-order relaxation of camber toward a target (unconditionally stable).
relaxCamber :: Double -> Double -> Double -> Double -> Double
relaxCamber dt tau current target
  | dt <= 0 = current
  | tau <= 0 = target
  | otherwise = current + (target - current) * (1 - exp (-dt / tau))

clamp01 :: Double -> Double
clamp01 value = max 0.0 (min 1.0 value)