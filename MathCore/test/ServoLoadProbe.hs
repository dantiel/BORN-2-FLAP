{-# LANGUAGE OverloadedStrings #-}
module Main (main) where

import Data.List (foldl')

import Born2Flap.Math.Types ( Vec3(..) )
import Born2Flap.Math.Vehicle
  ( addVec, crossVec, scaleVec, magnitude, zeroVec
  , VehicleOutput(..) )
import Born2Flap.Math.Firmware
  ( PilotInput(..), pilotToRc, defaultFirmwareParams, FirmwareParams(..)
  , FlightProfile(..), computeServoMixer, MixerOutput(..)
  , FirmwareState(..) )
import Born2Flap.Math.Servo
  ( ServoSpec(..), BatterySpec(..), servoFromNominal, defaultBatterySpec
  , servoAngleDeg, servoRateDegPerSec )
import Born2Flap.Math.Model ( kestrelModel )
import Born2Flap.Math.FirmwareVehicle
  ( FirmwareVehicleState(..), defaultFirmwareVehicleState
  , stepFirmwareVehicleWithMassInertia )
import Born2Flap.Math.Airframe ( airframeInertia )

data Quat = Quat !Double !Double !Double !Double

quatMul :: Quat -> Quat -> Quat
quatMul (Quat a1 b1 c1 d1) (Quat a2 b2 c2 d2) = Quat
  (a1*a2 - b1*b2 - c1*c2 - d1*d2)
  (a1*b2 + b1*a2 + c1*d2 - d1*c2)
  (a1*c2 - b1*d2 + c1*a2 + d1*b2)
  (a1*d2 + b1*c2 - c1*b2 + d1*a2)

quatNorm :: Quat -> Quat
quatNorm (Quat a b c d) =
  let n = sqrt (a*a + b*b + c*c + d*d)
  in Quat (a/n) (b/n) (c/n) (d/n)

quatRot :: Quat -> Vec3 -> Vec3
quatRot (Quat a b c d) (Vec3 vx vy vz) =
  let t2 = a*b; t3 = a*c; t4 = a*d; t5 = -b*b; t6 = b*c; t7 = b*d
      t8 = -c*c; t9 = c*d; t10 = -d*d
      wx = 2*((t8+t10)*vx + (t6-t4)*vy + (t3+t7)*vz) + vx
      wy = 2*((t4+t6)*vx + (t5+t10)*vy + (t9-t2)*vz) + vy
      wz = 2*((t7-t3)*vx + (t2+t9)*vy + (t5+t8)*vz) + vz
  in Vec3 wx wy wz

quatInvRot :: Quat -> Vec3 -> Vec3
quatInvRot (Quat a b c d) v = quatRot (Quat a (-b) (-c) (-d)) v

stepQuat :: Quat -> Vec3 -> Double -> Quat
stepQuat q (Vec3 px py pz) dt =
  let half = 0.5 * dt
  in quatNorm (quatMul q (Quat 1 (half*px) (half*py) (half*pz)))

data Sim = Sim
  { sQuat    :: !Quat
  , sVGround :: !Vec3
  , sOmega   :: !Vec3
  , sFv      :: !FirmwareVehicleState
  , sT       :: !Double
  }

massKg :: Double
massKg = 0.050

rollInertia :: Double
rollInertia = 0.00074484

(ixD, iyD, izD) = airframeInertia kestrelModel massKg 0

dt :: Double
dt = 1.0 / 240.0

-- Live game's Kestrel config: mirrors b2f_math_create_firmware_vehicle
-- (FFI.hs) followed by applyTuning (Born2FlapTuning.h model 2).
gameParams :: FirmwareParams
gameParams =
  let created = defaultFirmwareParams
        { fwServoSpeedMs = 60000 / 750
        , fwFlapBaseFreqDh = 32
        , fwProfile = (fwProfile defaultFirmwareParams)
            { profThrottleFrequencyMix = 100
            , profAileronSkewMix = 10
            , profRudderAmplitudeDiff = 100
            , profRudderFerocityRange = 20
            , profAileronScale = 10
            , profElevatorScale = 55
            }
        }
      tuned = (fwProfile created)
        { profTailElevatorAngleDeg = -2.3
        , profGlideAngleDeg = -4
        , profStrokeFerocity = 50
        , profReturnFerocity = 50
        , profAileronScale = 40
        , profElevatorScale = 55
        , profRudderAmplitudeDiff = 100
        , profStrokeSkew = 0
        , profReturnSkew = 0
        }
  in created { fwFlapCentreDeg = 0
             , fwVerticalMountIncidenceDeg = 0
             , fwProfile = tuned }

gameServo :: ServoSpec
gameServo = servoFromNominal "kestrel-game" 750 1.332 20

gameBattery :: BatterySpec
gameBattery = defaultBatterySpec
  { batteryNominalVoltage = 11.1
  , batteryInternalResistanceOhm = 0.08
  , batteryCapacityAh = 1.3
  , batteryCRating = 30
  , batteryBurstCRating = 60
  }

stepSim :: Double -> PilotInput -> Sim -> (Sim, VehicleOutput)
stepSim = stepSim' gameParams

stepSim' :: FirmwareParams -> Double -> PilotInput -> Sim -> (Sim, VehicleOutput)
stepSim' params step pilot sim =
  let bodyUp = quatInvRot (sQuat sim) (Vec3 0 0 1)
      vAirB = quatInvRot (sQuat sim) (sVGround sim)   -- body-frame, like the bridge
      rc = pilotToRc pilot
      (out, st') = stepFirmwareVehicleWithMassInertia
        massKg rollInertia True rc params gameServo
        gameBattery kestrelModel step vAirB (sOmega sim) 1000 bodyUp (sFv sim)
      fB = totalForceN out
      mB = totalMomentNm out
      fW = quatRot (sQuat sim) fB
      vG' = addVec (sVGround sim)
              (scaleVec step (addVec (scaleVec (1/massKg) fW) (Vec3 0 0 (-9.81))))
      mOver = Vec3 (x mB / ixD) (y mB / iyD) (z mB / izD)
      omega' = addVec (sOmega sim) (scaleVec step mOver)
      q' = stepQuat (sQuat sim) omega' step
  in ( Sim { sQuat = q', sVGround = vG', sOmega = omega', sFv = st', sT = sT sim + step }
     , out )

pilotAt :: [(Double, Double, Double, Double, Double)] -> Double -> PilotInput
pilotAt script t =
  let (_,thr,roll,pitch,yaw) = last [p | p@(s,_,_,_,_) <- script, s <= t]
  in PilotInput thr roll pitch yaw 0.5 True

f3 :: Double -> String
f3 v =
  let s = if v < 0 then "-" else ""
      a = abs v
      whole = truncate a :: Int
      frac = truncate ((a - fromIntegral whole) * 1000 + 0.5) :: Int
      fracS = replicate (3 - length (show frac)) '0' ++ show frac
  in s ++ show whole ++ "." ++ take 3 fracS

fmtV :: Vec3 -> String
fmtV (Vec3 a b c) = "(" ++ f3 a ++ "," ++ f3 b ++ "," ++ f3 c ++ ")"

-- | Commanded mixer angles for the CURRENT rc, mirroring the firmware call.
commandAt :: PilotInput -> FirmwareVehicleState -> (Double, Double)
commandAt = commandAt' gameParams

commandAt' :: FirmwareParams -> PilotInput -> FirmwareVehicleState -> (Double, Double)
commandAt' params pilot st =
  let rc = pilotToRc pilot
      (mix, _) = computeServoMixer True rc params (fvFirmware st) dt
  in (mixLeftFlapDevDeg mix, mixRightFlapDevDeg mix)

runScenario :: String -> [(Double, Double, Double, Double, Double)] -> Double -> IO ()
runScenario label script seconds = do
  putStrLn $ "\n=== " ++ label ++ " (dt=" ++ f3 dt ++ ") ==="
  let sim0 = Sim (Quat 1 0 0 0) (Vec3 8 0 0.3) zeroVec
        (defaultFirmwareVehicleState { fvMassFrame = Just (0, iyD, izD) }) 0
      nSteps = round (seconds / dt) :: Int
      printEvery = 12  -- 20 Hz logging
  let loop :: Int -> Sim -> IO ()
      loop n sim | n <= 0 = pure ()
                 | otherwise =
        let t = sT sim
            pilot = pilotAt script t
            (cmdL, cmdR) = commandAt pilot (sFv sim)
            (sim', out) = stepSim dt pilot sim
            st' = sFv sim'
            aL = servoAngleDeg (fvServoLeft st')
            aR = servoAngleDeg (fvServoRight st')
            rL = servoRateDegPerSec (fvServoLeft st')
            rR = servoRateDegPerSec (fvServoRight st')
            hL = fvLeftHingeTorqueNm st'
            hR = fvRightHingeTorqueNm st'
            errL = aL - cmdL
            errR = aR - cmdR
        in do
          if n `mod` printEvery == 0
            then putStrLn $ "t=" ++ f3 t
              ++ " cmd=" ++ f3 cmdL ++ "/" ++ f3 cmdR
              ++ " act=" ++ f3 aL ++ "/" ++ f3 aR
              ++ " err=" ++ f3 errL ++ "/" ++ f3 errR
              ++ " rate=" ++ f3 rL ++ "/" ++ f3 rR
              ++ " hingeNm=" ++ f3 hL ++ "/" ++ f3 hR
              ++ " F=" ++ fmtV (totalForceN out)
              ++ " M=" ++ fmtV (totalMomentNm out)
              ++ " omega=" ++ fmtV (sOmega sim')
              ++ " flags=" ++ show (outputFlags out)
            else pure ()
          loop (n-1) sim'
  loop nSteps sim0

-- | Banked HOVER (no forward speed): the static hang must roll the body
-- back toward level while the wings hold the fuselage's weight.
runHoverBankScenario :: Double -> Double -> IO ()
runHoverBankScenario bankDeg seconds = do
  putStrLn $ "\n=== HANG-HOVER (bank=" ++ show bankDeg ++ "deg, thr 0.5, dt=" ++ f3 dt ++ ") ==="
  let half = bankDeg * pi / 360
      q0 = Quat (cos half) (sin half) 0 0
      sim0 = Sim q0 zeroVec zeroVec
        (defaultFirmwareVehicleState { fvMassFrame = Just (0, iyD, izD) }) 0
      nSteps = round (seconds / dt) :: Int
      printEvery = 12
  let loop :: Int -> Sim -> IO ()
      loop n sim | n <= 0 = pure ()
                 | otherwise =
        let t = sT sim
            pilot = pilotAt [(0, 0.5, 0, 0, 0)] t
            (cmdL, cmdR) = commandAt pilot (sFv sim)
            (sim', out) = stepSim dt pilot sim
            st' = sFv sim'
            aL = servoAngleDeg (fvServoLeft st')
            aR = servoAngleDeg (fvServoRight st')
            errL = aL - cmdL
            errR = aR - cmdR
            up = quatInvRot (sQuat sim') (Vec3 0 0 1)
        in do
          if n `mod` printEvery == 0
            then putStrLn $ "t=" ++ f3 t
              ++ " up=" ++ fmtV up
              ++ " vG=" ++ fmtV (sVGround sim')
              ++ " err=" ++ f3 errL ++ "/" ++ f3 errR
              ++ " F=" ++ fmtV (totalForceN out)
              ++ " M=" ++ fmtV (totalMomentNm out)
              ++ " omega=" ++ fmtV (sOmega sim')
              ++ " flags=" ++ show (outputFlags out)
            else pure ()
          loop (n-1) sim'
  loop nSteps sim0
-- | Start from a BANKED attitude (world up tilted in body frame), glide,
-- no stick: the hang pendulum must roll the body back toward level in
-- dihedral; in anhedral the same hang force leads to sideslip instead.
runBankedScenario :: String -> Double -> Double -> Double -> IO ()
runBankedScenario label bankDeg mountDeg seconds = do
  putStrLn $ "\n=== " ++ label ++ " (bank=" ++ show bankDeg
    ++ "deg, mount=" ++ show mountDeg ++ "deg, dt=" ++ f3 dt ++ ") ==="
  let half = bankDeg * pi / 360
      q0 = Quat (cos half) (sin half) 0 0
      params' = gameParams { fwVerticalMountIncidenceDeg = mountDeg }
      sim0 = Sim q0 (Vec3 8 0 0.3) zeroVec
        (defaultFirmwareVehicleState { fvMassFrame = Just (0, iyD, izD) }) 0
      nSteps = round (seconds / dt) :: Int
      printEvery = 12  -- 20 Hz logging
  let loop :: Int -> Sim -> IO ()
      loop n sim | n <= 0 = pure ()
                 | otherwise =
        let t = sT sim
            pilot = pilotAt [(0, 0.0, 0, 0, 0)] t
            (cmdL, cmdR) = commandAt' params' pilot (sFv sim)
            (sim', out) = stepSim' params' dt pilot sim
            st' = sFv sim'
            aL = servoAngleDeg (fvServoLeft st')
            aR = servoAngleDeg (fvServoRight st')
            errL = aL - cmdL
            errR = aR - cmdR
            up = quatInvRot (sQuat sim') (Vec3 0 0 1)
        in do
          if n `mod` printEvery == 0
            then putStrLn $ "t=" ++ f3 t
              ++ " up=" ++ fmtV up
              ++ " vG=" ++ fmtV (sVGround sim')
              ++ " err=" ++ f3 errL ++ "/" ++ f3 errR
              ++ " F=" ++ fmtV (totalForceN out)
              ++ " M=" ++ fmtV (totalMomentNm out)
              ++ " omega=" ++ fmtV (sOmega sim')
              ++ " flags=" ++ show (outputFlags out)
            else pure ()
          loop (n-1) sim'
  loop nSteps sim0


main :: IO ()
main = do
  putStrLn ("airframeInertia (ix,iy,iz) = " ++ show (ixD, iyD, izD))
  runHoverBankScenario 15 4
  runBankedScenario "HANG-PENDULUM DIHEDRAL (glide 8 m/s, banked 15deg, mount +10: must self-right)" 15 10 6
  runBankedScenario "HANG-PENDULUM NEUTRAL (glide 8 m/s, banked 15deg, mount 0)" 15 0 6
  runBankedScenario "HANG-PENDULUM ANHEDRAL (glide 8 m/s, banked 15deg, mount -20: must slip, no self-right)" 15 (-20) 6
  runScenario "CRUISE-FLAP (thr 0.5, 8s: tracking + body reaction)"
    [ (0, 0.5, 0, 0, 0) ] 8
  runScenario "ROLL+PITCH COMBINED (thr 0.5, roll 0.4 + pitch 0.5 at t=4: wing diff banks, fixed tail yaws)"
    [ (0, 0.5, 0,    0, 0)
    , (4, 0.5, 0.4, 0.5, 0) ] 8
  runScenario "FULL-THROTTLE CRUISE (thr 1.0, 6s: servo envelope)"
    [ (0, 1.0, 0, 0, 0) ] 6
  runScenario "FULL-THROTTLE ROLL+PITCH (thr 1.0, roll 0.4 + pitch 0.3 at t=3)"
    [ (0, 1.0, 0,   0, 0)
    , (3, 1.0, 0.4, 0.3, 0) ] 6