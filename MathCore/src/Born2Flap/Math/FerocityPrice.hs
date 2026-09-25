-- | Ferocity-price helpers (Rubedo P4, audit-verified 9.5).
--
-- The dwell fraction @d@ is the one number the waveform shapes around; the
-- price paid for holding a stance in the flapping fluidum scales as a power
-- of @1\/(1-d)@. These are pure scalings — no absolute watt/force values —
-- frozen as regression references by the Rubedo spec §3.1.
module Born2Flap.Math.FerocityPrice
  ( dwellFromFerocity
  , ferocityPowerPrice, ferocityForcePrice, ferocityThrustPrice
  ) where

-- | d = ferocity01 · kWaveMaxDwell, ferocity01 = f·0.125, kWaveMaxDwell = 0.98.
-- Faithful to @Waveform.hs@: dwell is ferocity (0..8) scaled to [0, 0.98].
dwellFromFerocity :: Double -> Double
dwellFromFerocity f = clamp 0 8 f * 0.125 * 0.98

-- | d → power 1/(1-d)³.
ferocityPowerPrice :: Double -> Double
ferocityPowerPrice d = 1 / (1 - d) ** 3

-- | d → force/torque 1/(1-d)².
ferocityForcePrice :: Double -> Double
ferocityForcePrice d = 1 / (1 - d) ** 2

-- | d → thrust 1/(1-d).
ferocityThrustPrice :: Double -> Double
ferocityThrustPrice d = 1 / (1 - d)

clamp :: Ord a => a -> a -> a -> a
clamp low high = max low . min high
