{-# LANGUAGE DerivingStrategies #-}
{-# LANGUAGE StrictData #-}
-- | Read-only tracking-phase diagnostics. This is NOT physical resonance:
-- there is no elastic inertia, spring energy exchange, or mechanical reaction
-- in this estimator. It must not alter the servo motion waveform.
module Born2Flap.Math.ResponsePhase
  ( ResponsePhaseState(..), defaultResponsePhase, stepResponsePhase ) where

data ResponsePhaseState = ResponsePhaseState
  { rpReferenceCos :: !Double, rpReferenceSin :: !Double
  , rpResponseCos :: !Double, rpResponseSin :: !Double
  , rpPhaseError :: !Double
  , rpEngagement :: !Double
  } deriving stock (Eq, Show)

defaultResponsePhase :: ResponsePhaseState
defaultResponsePhase = ResponsePhaseState 0 0 0 0 0 0

-- | Command/response in degrees, actual oscillator phase and omega in rad/s.
-- Reports response phase relative to command (negative for a delayed response)
-- and confidence, with no control output.
stepResponsePhase :: Double -> Double -> Double -> Double -> Double
              -> ResponsePhaseState -> ResponsePhaseState
stepResponsePhase command response phase omega dt old
  | dt <= 0 = old
  | omega < 0.1 = defaultResponsePhase
  | otherwise =
    let tau = max 0.25 (2*pi/omega)
        a = 1-exp(-dt/tau)
        lp before sample = before+a*(sample-before)
        c = 2*cos phase; s = 2*sin phase
        rc = lp (rpReferenceCos old) (command*c)
        ri = lp (rpReferenceSin old) (command*s)
        yc = lp (rpResponseCos old) (response*c)
        yi = lp (rpResponseSin old) (response*s)
        amplitude = min (sqrt(rc*rc+ri*ri)) (sqrt(yc*yc+yi*yi))
        confidence = max 0 (min 1 ((amplitude-1)/4))
        difference = atan2 ri rc-atan2 yi yc
        lag = atan2 (sin difference) (cos difference)
        -- Large phase reversal is not a useful fundamental tracking signal.
        reliable = confidence * max 0 (cos lag)
    in ResponsePhaseState rc ri yc yi lag reliable
