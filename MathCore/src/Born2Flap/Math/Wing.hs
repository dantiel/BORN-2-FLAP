{-# LANGUAGE StrictData #-}

module Born2Flap.Math.Wing
  ( crossflowBaseline
  , relaxSeparation
  ) where

import Born2Flap.Math.Types

crossflowBaseline :: Double -> Radians -> MetresPerSecond -> MetresPerSecond
crossflowBaseline gain (Radians sweepAngle) (MetresPerSecond localSpeed) =
  MetresPerSecond (gain * sin sweepAngle * localSpeed)

relaxSeparation :: Seconds -> Seconds -> UnitInterval -> UnitInterval -> UnitInterval
relaxSeparation (Seconds dt) (Seconds timeConstant)
  (UnitInterval current) (UnitInterval target) =
    let safeTau = max 1.0e-6 timeConstant
        relaxation = 1.0 - exp (-dt / safeTau)
    in clamp01 (current + relaxation * (target - current))
