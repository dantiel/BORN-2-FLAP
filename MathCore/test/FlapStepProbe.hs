{-# LANGUAGE OverloadedStrings #-}
module Main (main) where

import Born2Flap.Math.Types ( Vec3(..) )
import Born2Flap.Math.Firmware
  ( PilotInput(..), pilotToRc, defaultFirmwareParams, FirmwareParams(..)
  , FlightProfile(..) )
import Born2Flap.Math.Servo
  ( ServoSpec(..), BatterySpec(..), servoFromNominal, defaultBatterySpec )
import Born2Flap.Math.Model ( kestrelModel )
import Born2Flap.Math.FirmwareVehicle
  ( FirmwareVehicleState(..), defaultFirmwareVehicleState
  , stepFirmwareVehicleWithMassInertia )
import Born2Flap.Math.Servo ( servoAngleDeg )

main :: IO ()
main = do
  putStrLn "native flap probe start"
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
      pilot = PilotInput 0.72 0 0 0 0.56 True
      rc = pilotToRc pilot
      dt = 1.0 / 240.0
      go i st
        | i >= 10 = putStrLn "ALL 10 FLAP STEPS OK (native)"
        | otherwise = do
            let (out, st') = stepFirmwareVehicleWithMassInertia 0.050 0.01 True rc
                  params servo battery kestrelModel dt (Vec3 8 0 0) (Vec3 0 0 0)
                  1000 (Vec3 0 0 1) st
                l = servoAngleDeg (fvServoLeft st')
                r = servoAngleDeg (fvServoRight st')
            putStrLn (show i ++ "  L=" ++ show l ++ "  R=" ++ show r)
            go (i + 1) st'
  go (0 :: Int) defaultFirmwareVehicleState