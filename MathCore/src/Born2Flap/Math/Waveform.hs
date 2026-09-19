{-# LANGUAGE DerivingStrategies #-}
{-# LANGUAGE StrictData #-}

-- | Flapping oscillator: phase accumulator with Josephson-washboard
-- beat-locking plus an asymmetric dwell/pointed waveform.
--
-- Faithful port of PteronautOS
-- @src/lib/Ornithopter/OrnithopterWaveform.h@
-- (@FlappingOscillator::advance@ \/ @shapeWave@). The C++ reference uses
-- PROGMEM lookup tables (an ESP8266 no-FPU optimisation); here the same
-- continuous functions are evaluated directly:
--
--   * @waveCosHalfLerp(u)    = cos(pi * u)@
--   * @wavePointedLerp(k, t) = asin(k * cos(pi * t)) / asin(k)@
--
-- so this module is mathematically identical to (and slightly more accurate
-- than) the LUT-interpolated reference, with zero tables to carry.
module Born2Flap.Math.Waveform
  ( OscillatorState(..)
  , defaultOscillator
  , advanceOscillator
  , limiarFromFerocities
  , shapeWave
  , shapeWaveWithDerivative
  ) where

-- ── Oscillator state ───────────────────────────────────────────────
--
-- Matches the C++ @FlappingOscillator@ members one-to-one. The phase is the
-- /flap phase/ [rad]; @oscCadence@ is the instantaneous flap rate [rad\/s]
-- (base beat + debt momentum).

data OscillatorState = OscillatorState
  { oscPhase :: !Double        -- ^ actual flap phase [rad], kept in [0, 2π)
  , oscCadence :: !Double      -- ^ instantaneous flap rate [rad/s] (base + debt)
  , oscCadenceTarget :: !Double -- ^ commanded base flap rate [rad/s] — the beat grid
  , oscKGainMod :: !Double     -- ^ ONDAS phase-advance demand (1.0 = nominal)
  , oscAnchorGain :: !Double   -- ^ beat-locking stiffness (0 soft … 100 stiff)
  , oscBasePhase :: !Double    -- ^ virtual beat grid [rad]
  , oscPhaseOffset :: !Double  -- ^ debt to the grid [rad] — settles on whole strokes
  , oscDebtVel :: !Double      -- ^ debt momentum [rad/s]
  } deriving stock (Eq, Show)

defaultOscillator :: OscillatorState
defaultOscillator = OscillatorState 0 0 0 1 0 0 0 0

twoPi :: Double
twoPi = 2 * pi

-- fmodf semantics: x - trunc(x / m) * m, result sign follows x, then wrap.
wrap2pi :: Double -> Double
wrap2pi x =
  let q = truncate (x / twoPi) :: Integer
      r = x - fromInteger q * twoPi
  in if r < 0 then r + twoPi else r

-- ── Phase accumulation ─────────────────────────────────────────────
--
-- Josephson washboard: the debt @phaseOffset@ is attracted to whole strokes
-- (multiples of 2π) by @-ω₀²·sin(φ_offset)@. Weak demand only nudges the
-- phase and rings back to the same beat; a strong demand whips the debt over
-- the π barrier — a quantized phase slip of exactly one whole stroke — so the
-- flap always lands on a beat, never between beats.
--
--   φ_offset'' = -ω₀²·sin(φ_offset) - 2ζω₀·(φ_offset' - extraTarget)

advanceOscillator
  :: Double          -- ^ cadenceTarget [rad/s]
  -> Double          -- ^ kGainMod (ONDAS phase-advance demand)
  -> Double          -- ^ anchorGain (beat-locking stiffness)
  -> Double          -- ^ dt [s]
  -> OscillatorState
  -> (Double, OscillatorState)  -- ^ (phase, next state)
advanceOscillator cadenceTarget kGainMod anchorGain dt osc =
  let kBaseDamp = 10.0
      kZeta = 0.7
      omega0 = kBaseDamp + anchorGain
      basePhase = wrap2pi (oscBasePhase osc + cadenceTarget * dt)
      extraTarget = (kGainMod - 1) * cadenceTarget
      debtVel = oscDebtVel osc
              + (-omega0 * omega0 * sin (oscPhaseOffset osc)
                 - 2 * kZeta * omega0 * (oscDebtVel osc - extraTarget)) * dt
      phaseOffset = oscPhaseOffset osc + debtVel * dt
      cadence = cadenceTarget + debtVel
      phase = wrap2pi (basePhase + phaseOffset)
      next = OscillatorState phase cadence cadenceTarget kGainMod anchorGain
               basePhase phaseOffset debtVel
  in (phase, next)

-- ── Wave shaping ───────────────────────────────────────────────────

kWaveMaxDwell :: Double
kWaveMaxDwell = 0.98   -- never emit an impossible position jump

kWaveMaxPoint :: Double
kWaveMaxPoint = 0.98   -- rounded, never infinite-accel triangle

-- cos(π·u), u ∈ [0,1] → [1,−1] (the analytic form of waveCosHalfLerp).
cosHalf :: Double -> Double
cosHalf u = cos (pi * u)

-- Downstroke/upstroke boundary from the two ferocities. Shared between the
-- wings so left/right reverse at the SAME phase even when their ferocities
-- differ (rudder differential) — the C++ @limiarBase@.
limiarFromFerocities :: Double -> Double -> Double
limiarFromFerocities fD fS =
  let fDc = clamp 0 8 fD
      fSc = clamp 0 8 fS
      wD = max 0.01 (8 - fDc)
      wS = max 0.01 (8 - fSc)
  in twoPi * wD / (wD + wS)

-- | Asymmetric waveform: value only. @theta@ must already be normalised to
-- [0, 2π) (the only caller, @advanceOscillator@, guarantees this).
shapeWave
  :: Double  -- ^ theta (flap phase [rad], in [0, 2π))
  -> Double  -- ^ strokeFerocity (downstroke, 0..8)
  -> Double  -- ^ returnFerocity (upstroke, 0..8)
  -> Double  -- ^ limiarShared (>= 0 → shared reversal threshold; < 0 → compute)
  -> Double  -- ^ shapeMixPercent (0..100, square↔triangle transmute)
  -> Double  -- ^ strokeSkewPercent (±100, centre-skew of downstroke)
  -> Double  -- ^ returnSkewPercent (±100, centre-skew of upstroke)
  -> Double  -- ^ pulse ∈ [-1, +1]
shapeWave theta fD fS limiarShared shapeMixPercent strokeSkewPercent returnSkewPercent =
  fst (shapeWaveWithDerivative theta fD fS limiarShared shapeMixPercent strokeSkewPercent returnSkewPercent)

-- | Asymmetric waveform with its phase derivative @(pulse, d pulse / dθ)@.
-- The derivative is the instantaneous flap velocity @d(pulse)/dt@ scaled by
-- cadence; it is C¹-continuous across the dwell ramps and the half-stroke
-- reversal (both cosine ramps meet the plateaus with zero slope).
shapeWaveWithDerivative
  :: Double -> Double -> Double -> Double -> Double -> Double -> Double
  -> (Double, Double)
shapeWaveWithDerivative theta strokeFerocity returnFerocity limiarShared
  shapeMixPercent strokeSkewPercent returnSkewPercent =
  let fD = clamp 0 8 strokeFerocity
      fS = clamp 0 8 returnFerocity
      shapeMix = clamp 0 1 (shapeMixPercent * 0.01)
      limiar = if limiarShared >= 0 then limiarShared else limiarFromFerocities fD fS
      descida = theta < limiar
      (tRaw, f, skew01, dtRawDTheta) =
        if descida
          then (theta / limiar, fD, clamp (-1) 1 (strokeSkewPercent * 0.01), 1 / limiar)
          else ((theta - limiar) / (twoPi - limiar), fS
               , clamp (-1) 1 (returnSkewPercent * 0.01), 1 / (twoPi - limiar))
      -- Centre-skew: quadratic bias t' = t + s·t·(1−t), monotonic and
      -- end-point-preserving. dt/dt_raw below is its derivative.
      dtDtRaw = 1 + skew01 * (1 - 2 * tRaw)
      dtDTheta = dtDtRaw * dtRawDTheta
      t = tRaw + skew01 * tRaw * (1 - tRaw)
      ferocity01 = f * 0.125
      d = ferocity01 * kWaveMaxDwell
      dh = d * 0.5
      frontDwell = dh * (1 + skew01)
      backDwell = dh * (1 - skew01)
      (plateau, plateauDeriv)
        | t < frontDwell = (1, 0)
        | t > 1 - backDwell = (-1, 0)
        | otherwise =
            let u = (t - frontDwell) / (1 - d)
            in (cosHalf u, (-pi / (1 - d)) * sin (pi * u) * dtDTheta)
      pointK = kWaveMaxPoint * (2 * ferocity01 - ferocity01 * ferocity01)
      (pointed, pointedDeriv)
        | pointK < 0.0001 = (cosHalf t, -pi * sin (pi * t) * dtDTheta)
        | otherwise =
            let c = cos (pi * t)
                s = sin (pi * t)
                denom = max 1.0e-9 (sqrt (1 - pointK * pointK * c * c))
            in (asin (pointK * c) / asin pointK,
                (-pi * pointK * s * dtDTheta) / (asin pointK * denom))
      halfWave = plateau + (pointed - plateau) * shapeMix
      halfWaveDeriv = plateauDeriv * (1 - shapeMix) + pointedDeriv * shapeMix
      sign = if descida then 1 else -1
  in (sign * halfWave, sign * halfWaveDeriv)

clamp :: Ord a => a -> a -> a -> a
clamp low high = max low . min high
