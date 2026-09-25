{-# LANGUAGE DerivingStrategies #-}
{-# LANGUAGE StrictData #-}

-- | RESONANCE layer (Rubedo P7/P8/P9): Vold–Kalman engagement → phase error
-- → phase-advance demand. Pure functions; the loop threads the state.
--
-- The phase-lock parking well lives in the ROTATING (error) frame:
--
--   dδ/dt = η − 2κ·d·sin(2δ)
--
-- where δ is the phase error, η the wind phase noise, d the dwell fraction,
-- κ the coupling. The same restoring force becomes a cadence demand on top of
-- the washboard beat-lock (layered, never replacing it — P9): the oscillator
-- is advanced by kGainMod = 1 + (2κ·d·sin 2δ)·dt/ω.
module Born2Flap.Math.Resonance
  ( ResonanceState(..), defaultResonance
  , stepPhaseLockDwell, stepResonance
  , engagementFromTracked
  ) where

import Born2Flap.Math.OrderTracker

data ResonanceState = ResonanceState
  { rsTracker    :: !OrderTrackerState
  , rsPhaseError :: !Double
  , rsDwell      :: !Double
  , rsCoupling   :: !Double
  } deriving stock (Eq, Show)

defaultResonance :: ResonanceState
defaultResonance = ResonanceState defaultOrderTracker 0 0 8.0

-- | Parking well in the rotating (error) frame. NOTE the sin(2δ) — a
-- lab-frame sin(2θ) coupling pins the phase to a fixed angle and destabilizes
-- (frozen as a permanent "forbidden" regression, Citrinitas C.4.3).
stepPhaseLockDwell :: Double -> Double -> Double -> Double -> Double -> Double
stepPhaseLockDwell eta kappa d dt delta =
  delta + (eta - 2 * kappa * d * sin (2 * delta)) * dt

-- | Soft-knee engagement 0..1 of an in-band amplitude. Provisional scale:
-- a 10° phase-coherent servo error engages fully.
engagementFromTracked :: Double -> Double
engagementFromTracked a = a / (a + 10.0)

-- | One RESONANCE step. @measured@ = open-loop servo tracking error (deg),
-- @omega@ = instantaneous flap rate (rad/s), @eta@ = wind phase noise (rad/s).
-- Returns @(kGainMod demand, next state)@ — a phase-advance demand (default 1),
-- never a position command (P8).
stepResonance
  :: Double -> Double -> Double -> Double -> ResonanceState
  -> (Double, ResonanceState)
stepResonance measured omega eta dt st =
  let (tracked, nextTracker) = stepOrderTracker measured omega dt (rsTracker st)
      target = engagementFromTracked (abs tracked)
      d      = rsDwell st + (target - rsDwell st) * clamp 0 1 (dt / 0.25)
      kappa  = rsCoupling st
      delta  = stepPhaseLockDwell eta kappa d dt (rsPhaseError st)
      adv    = 2 * kappa * d * sin (2 * delta) * dt / max 0.1 omega
      kGainMod = clamp 0.5 1.5 (1 + adv)
  in (kGainMod, ResonanceState nextTracker delta d kappa)
  where
    clamp lo hi = max lo . min hi