{-# LANGUAGE DerivingStrategies #-}
{-# LANGUAGE StrictData #-}

-- | First-generation Vold–Kalman order tracker (Rubedo P6).
--
-- A 2nd-order resonator tuned to the instantaneous flap frequency @omega@
-- extracts the in-band (phase-coherent) component of an error signal. Faithful
-- to the OrniFlight reference @ondas_tracker.c@: state {x, v}, ζ = 0.06
-- (Q ≈ 8.3), order 1 → w = ω, bw = 2ζω, explicit Euler. Below
-- @orderTrackerOmegaMin@ the resonator idles (state reset to 0) so it cannot
-- drift on a stopped or gliding wing.
module Born2Flap.Math.OrderTracker
  ( OrderTrackerState(..), defaultOrderTracker
  , orderTrackerZeta, orderTrackerOmegaMin
  , stepOrderTracker
  ) where

orderTrackerZeta :: Double
orderTrackerZeta = 0.06        -- Q ≈ 8.3

orderTrackerOmegaMin :: Double
orderTrackerOmegaMin = 0.1     -- rad/s: idles below this

data OrderTrackerState = OrderTrackerState
  { otX :: !Double
  , otV :: !Double
  } deriving stock (Eq, Show)

defaultOrderTracker :: OrderTrackerState
defaultOrderTracker = OrderTrackerState 0 0

-- | Advance one sample. Returns @(tracked output, next state)@. The output is
-- the in-band component @x'@ of @sample@ at frequency @omega@.
stepOrderTracker :: Double -> Double -> Double -> OrderTrackerState
                 -> (Double, OrderTrackerState)
stepOrderTracker sample omega dt state
  | omega < orderTrackerOmegaMin || dt <= 0 = (0, defaultOrderTracker)
  | otherwise =
      let w  = omega
          bw = 2 * orderTrackerZeta * w
          v' = otV state + (-w * w * otX state - bw * otV state + bw * sample) * dt
          x' = otX state + v' * dt
      in (x', OrderTrackerState x' v')