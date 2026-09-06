module Main (main) where

import Born2Flap.Math.Types
import Born2Flap.Math.Wing
import Born2Flap.Math.Control
import Born2Flap.Math.Vehicle
import Born2Flap.Math.Simulation

main :: IO ()
main = do
  let rollback = runSimulation (putState (99 :: Int) >> abort "failure") 7
                   :: Either String ((), Int)
  if rollback == Left "failure" then pure () else fail "transaction must not expose failed state"
  let MetresPerSecond positive = crossflowBaseline 0.3 (Radians 0.4) (MetresPerSecond 8)
      MetresPerSecond negative = crossflowBaseline 0.3 (Radians (-0.4)) (MetresPerSecond 8)
  if positive > 0 && negative < 0 && abs (positive + negative) < 1.0e-12
    then pure ()
    else fail "crossflow must reverse with signed sweep"
  let initialController = AxisController
        { proportionalGain = 1.0
        , integralGain = 0.2
        , derivativeGain = 0.01
        , integralLimit = 0.5
        , integralState = 0.0
        , previousError = 0.0
        }
      result = stepAxis (Seconds 0.01) (AxisInput 1.0 0.0) initialController
  if command result > 0 && integralState (controller result) <= 0.5
    then pure ()
    else fail "controller must respond positively and respect its integral bound"
  let input = VehicleInput (1 / 240) (Vec3 5 0 0) (Vec3 0 0 0) 0.7 0 0 0
      samples = take 1200 (tail (iterate (\(_, state) -> stepVehicle input state)
                                   (zeroVehicleOutput, defaultVehicle)))
      outputs = map fst samples
      finite value = not (isNaN value || isInfinite value)
      allFinite output = all finite
        [ x (totalForceN output), y (totalForceN output), z (totalForceN output)
        , x (totalMomentNm output), y (totalMomentNm output), z (totalMomentNm output)
        , totalMechanicalPowerW output, maxSeparation output
        ]
  if all allFinite outputs && all (\output -> maxSeparation output >= 0 && maxSeparation output <= 1) outputs
    then pure ()
    else fail "vehicle loads must remain finite and separation bounded"
  let (_, warmed) = last samples
      changedThrottle = input { throttleCommand = 0.1 }
      (_, changedState) = stepVehicle changedThrottle warmed
      expectedPhase = vehiclePhase warmed + stepSeconds input * 2 * pi * (1.2 + 3.8 * 0.1)
      phaseError = atan2 (sin (vehiclePhase changedState - expectedPhase))
                         (cos (vehiclePhase changedState - expectedPhase))
      invalidInputs =
        [ input { stepSeconds = 0 }, input { stepSeconds = 0.06 }
        , input { throttleCommand = 0 / 0 }, input { bodyRatesRadS = Vec3 (1 / 0) 0 0 }
        , input { bodyVelocityMS = Vec3 0 (0 / 0) 0 }
        ]
  if abs phaseError < 1e-12 && abs (vehiclePhase changedState) <= pi
    then pure () else fail "throttle change must integrate a bounded continuous phase"
  mapM_ (\bad -> let (rejected, unchanged) = stepVehicle bad warmed
                 in if outputFlags rejected /= 0 && unchanged == warmed
                    then pure () else fail "invalid input must roll back all state") invalidInputs
  let stationary = input { bodyVelocityMS = Vec3 0 0 0, pitchCommand = 0, yawCommand = 0 }
      (neutral, _) = stepVehicle stationary defaultVehicle
      (deflected, _) = stepVehicle (stationary { pitchCommand = 1, yawCommand = 1 }) defaultVehicle
  if totalForceN neutral == totalForceN deflected && totalMomentNm neutral == totalMomentNm deflected
    then pure () else fail "stationary tail must not generate control forces without flow"
  if all (\o -> abs (y (totalForceN o)) < 1e-10 && abs (x (totalMomentNm o)) < 1e-10
                && abs (z (totalMomentNm o)) < 1e-10) outputs
    then pure () else fail "symmetric wings must cancel lateral loads and roll/yaw moments"
  let rollInput = input { rollCommand = 0.8 }
      (rollOutput, _) = stepVehicle rollInput defaultVehicle
  if abs (x (totalMomentNm rollOutput)) > 1.0e-6
    then putStrLn "MathCore properties passed"
    else fail "differential flapping must create a roll moment"

zeroVehicleOutput :: VehicleOutput
zeroVehicleOutput = VehicleOutput (Vec3 0 0 0) (Vec3 0 0 0) 0 0 (-1) 0
