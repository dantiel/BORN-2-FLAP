{-# LANGUAGE DerivingStrategies #-}
{-# LANGUAGE StrictData #-}

module Born2Flap.Math.Control
  ( AxisController(..)
  , AxisInput(..)
  , AxisOutput(..)
  , stepAxis
  ) where

import Born2Flap.Math.Types (Seconds(..))

data AxisController = AxisController
  { proportionalGain :: !Double
  , integralGain :: !Double
  , derivativeGain :: !Double
  , integralLimit :: !Double
  , integralState :: !Double
  , previousError :: !Double
  } deriving stock (Eq, Show)

data AxisInput = AxisInput
  { setpoint :: !Double
  , measurement :: !Double
  } deriving stock (Eq, Show)

data AxisOutput = AxisOutput
  { command :: !Double
  , controller :: !AxisController
  } deriving stock (Eq, Show)

stepAxis :: Seconds -> AxisInput -> AxisController -> AxisOutput
stepAxis (Seconds dt) input state =
  let error = setpoint input - measurement input
      nextIntegral = clamp (negate (integralLimit state)) (integralLimit state)
        (integralState state + error * dt)
      derivative = if dt > 0 then (error - previousError state) / dt else 0
      output = proportionalGain state * error
             + integralGain state * nextIntegral
             + derivativeGain state * derivative
      nextState = state
        { integralState = nextIntegral
        , previousError = error
        }
  in AxisOutput output nextState
  where
    clamp low high = max low . min high
