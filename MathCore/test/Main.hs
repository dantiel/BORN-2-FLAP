module Main (main) where

import Born2Flap.Math.Types
import Born2Flap.Math.Wing
import Born2Flap.Math.Control
import Born2Flap.Math.Vehicle

main :: IO ()
main = do
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
  let rollInput = input { rollCommand = 0.8 }
      (rollOutput, _) = stepVehicle rollInput defaultVehicle
  if abs (x (totalMomentNm rollOutput)) > 1.0e-6
    then putStrLn "MathCore properties passed"
    else fail "differential flapping must create a roll moment"

zeroVehicleOutput :: VehicleOutput
zeroVehicleOutput = VehicleOutput (Vec3 0 0 0) (Vec3 0 0 0) 0 0 (-1) 0
