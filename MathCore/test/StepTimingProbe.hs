{-# LANGUAGE OverloadedStrings #-}
{-# LANGUAGE BangPatterns #-}
module Main (main) where

import Born2Flap.Math.Types ( Vec3(..) )
import Born2Flap.Math.Firmware
  ( PilotInput(..), pilotToRc, defaultFirmwareParams, FirmwareParams(..)
  , FlightProfile(..) )
import Born2Flap.Math.Servo
  ( ServoSpec(..), BatterySpec(..), servoFromNominal, defaultBatterySpec, servoAngleDeg )
import Born2Flap.Math.Model ( kestrelModel )
import Born2Flap.Math.FirmwareVehicle
  ( FirmwareVehicleState(..), defaultFirmwareVehicleState
  , stepFirmwareVehicleWithMassInertia )
import System.CPUTime
import System.Environment ( getArgs )

main :: IO ()
main = do
  args <- getArgs
  let n = case args of (a:_) -> read a :: Int; _ -> 24000
  putStrLn ("step timing probe start n=" ++ show n)
  let prof = (fwProfile defaultFirmwareParams)
        { profTailElevatorAngleDeg = -2.3
        , profGlideAngleDeg = -4
        , profStrokeFerocity = 50
        , profReturnFerocity = 50
        , profAileronScale = 40
        , profElevatorScale = 55
        , profStrokeSkew = 0
        , profReturnSkew = 0
        }
      params = defaultFirmwareParams
        { fwServoSpeedMs = 80, fwFlapBaseFreqDh = 32, fwFlapCentreDeg = 0
        , fwVerticalMountIncidenceDeg = 0, fwProfile = prof }
      servo = servoFromNominal "kestrel-game" 750 1.332 20
      battery = defaultBatterySpec
      pilot = PilotInput 0.72 0.15 (-0.1) 0.2 0.56 True
      rc = pilotToRc pilot
      dt = 1.0 / 240.0
      loop !i !st !acc
        | i >= n = pure acc
        | otherwise =
            let (_, st') = stepFirmwareVehicleWithMassInertia 0.050 0.01 True rc
                  params servo battery kestrelModel dt (Vec3 8 0 0) (Vec3 0.4 0 0)
                  1000 (Vec3 0 0 1) st
                !a = acc + servoAngleDeg (fvServoLeft st') + servoAngleDeg (fvServoRight st')
            in loop (i + 1) st' a
  -- warmup
  _ <- loop 0 defaultFirmwareVehicleState 0
  start <- getCPUTime
  acc <- loop 0 defaultFirmwareVehicleState 0
  end <- getCPUTime
  let us = fromIntegral (end - start) / 1.0e12
      perStepUs = us / fromIntegral n * 1.0e6
      hz = 1.0 / (us / fromIntegral n)
  putStrLn ("checksum=" ++ show acc)
  putStrLn ("total=" ++ show (us :: Double) ++ " s over " ++ show n ++ " steps")
  putStrLn ("per-step=" ++ show (perStepUs :: Double) ++ " us  =>  " ++ show (hz :: Double) ++ " Hz single-threaded")