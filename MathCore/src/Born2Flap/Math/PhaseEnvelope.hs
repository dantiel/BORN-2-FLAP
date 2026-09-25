{-# LANGUAGE DerivingStrategies #-}
{-# LANGUAGE StrictData #-}

-- | Phase-envelope layer (Rubedo P1/P2/P3).
--
-- A 16-bin Poincaré histogram strobed by the golden angle. The strobe never
-- falls on the same harmonic twice, so an error quantity sampled at reversals
-- is read uniformly over the flap cycle instead of aliasing a single overtone
-- into a standing offset. The golden-angle step is ⌊φ⁻¹·16·2¹⁶⌋ = 648095
-- (gcd(648095, 2²⁰) = 1), the same accumulator the OrniFlight reference uses.
module Born2Flap.Math.PhaseEnvelope
  ( goldenAngleStep, phaseBins, phaseEma
  , PhaseEnvelopeState(..), defaultPhaseEnvelope
  , PhaseEnvelopeMetric(..)
  , phaseStrobe, phaseEnvelope, phaseCoverage
  , detectReversal
  ) where

import Data.Bits
import Data.Word

-- ⌊φ⁻¹·16·2^16⌋ = 648095 (0x9E39F). gcd(648095, 2^20)=1.
goldenAngleStep :: Word32
goldenAngleStep = 648095

phaseBins :: Int
phaseBins = 16

phaseEma :: Double
phaseEma = 0.0625  -- 1/16 per strobe

data PhaseEnvelopeState = PhaseEnvelopeState
  { peAccum :: !Word32
  , peMeans :: ![Double]
  , peMask  :: !Word16
  } deriving stock (Eq, Show)

defaultPhaseEnvelope :: PhaseEnvelopeState
defaultPhaseEnvelope = PhaseEnvelopeState 0 (replicate phaseBins 0) 0

data PhaseEnvelopeMetric = PhaseEnvelopeMetric
  { peSupMean  :: !Double
  , peCoverage :: !Double
  } deriving stock (Eq, Show)

phaseStrobe :: Double -> PhaseEnvelopeState -> PhaseEnvelopeState
phaseStrobe errorAbs state =
  let acc'    = peAccum state + goldenAngleStep
      bin     = fromIntegral ((acc' `shiftR` 16) .&. 0xF)
      means'  = [ m * (1 - phaseEma) | m <- peMeans state ]
      means'' = addAt bin (errorAbs * phaseEma) means'
  in PhaseEnvelopeState acc' means'' (peMask state .|. (1 `shiftL` bin))
  where
    addAt i v xs = take i xs ++ [xs !! i + v] ++ drop (i + 1) xs

phaseEnvelope :: PhaseEnvelopeState -> Double
phaseEnvelope = maximum . map abs . peMeans

phaseCoverage :: PhaseEnvelopeState -> Double
phaseCoverage state =
  fromIntegral (popCount (peMask state)) / fromIntegral phaseBins

-- | Sinusoid-Nulldurchgang (P3): sign change across the down/upstroke boundary
-- @limiar@, plus the 2π wrap. Faithful to pid.c:1846 semantics via the
-- @limiar@ phase boundary (the stroke reversal point in [0, 2π)).
detectReversal :: Double -> Double -> Double -> Bool
detectReversal prev curr limiar =
  (prev < limiar && curr >= limiar) || (curr < prev)