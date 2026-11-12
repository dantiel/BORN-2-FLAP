{-# LANGUAGE OverloadedStrings #-}
module Main (main) where

import Data.List (foldl')

import Born2Flap.Math.Types ( Vec3(..) )
import Born2Flap.Math.Vehicle
  ( addVec, crossVec, scaleVec, magnitude, zeroVec
  , VehicleOutput(..) )
import Born2Flap.Math.Firmware
  ( PilotInput(..), pilotToRc, defaultFirmwareParams, FirmwareParams(..)
  , FlightProfile(..) )
import Born2Flap.Math.Servo
  ( ServoSpec(..), BatterySpec(..), servoFromNominal, defaultBatterySpec
  , servoAngleDeg )
import Born2Flap.Math.Model ( kestrelModel )
import Born2Flap.Math.FirmwareVehicle
  ( FirmwareVehicleState(..), defaultFirmwareVehicleState
  , stepFirmwareVehicleWithMassInertia, TailField(..) )
import Born2Flap.Math.Airframe ( airframeInertia )

-- Minimal quaternion (w,x,y,z) for the host-side integrator.
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

-- Game dryInertia X (FlightComponentMass log): 0.001
rollInertia :: Double
rollInertia = 0.001

(ixD, iyD, izD) = airframeInertia kestrelModel massKg 0

dt :: Double
dt = 1.0 / 240.0

-- | The live game's Kestrel tuning profile EXACTLY as flown (prefs model 2).
gameParams :: FirmwareParams
gameParams =
  let base = defaultFirmwareParams
      prof = (fwProfile base)
        { profTailElevatorAngleDeg = -6.1
        , profGlideAngleDeg = -4
        , profStrokeFerocity = 50
        , profReturnFerocity = 50
        , profAileronScale = 40
        , profElevatorScale = 55
        , profRudderAmplitudeDiff = 100
        , profStrokeSkew = 0
        , profReturnSkew = 0
        }
  in base { fwServoSpeedMs = 80, fwFlapBaseFreqDh = 32, fwFlapCentreDeg = 0
          , fwVerticalMountIncidenceDeg = 0, fwProfile = prof }

gameServo :: ServoSpec
gameServo = servoFromNominal "kestrel-game" 750 0.74 20

gameBattery :: BatterySpec
gameBattery = defaultBatterySpec
  { batteryNominalVoltage = 11.1
  , batteryInternalResistanceOhm = 0.08
  , batteryCapacityAh = 1.3
  , batteryCRating = 30
  , batteryBurstCRating = 60
  }

stepSim :: Double -> PilotInput -> Vec3 -> Sim -> (Sim, VehicleOutput)
stepSim step pilot wind sim =
  let bodyUp = quatInvRot (sQuat sim) (Vec3 0 0 1)
      vAir = addVec (sVGround sim) (scaleVec (-1) wind)
      vAirB = quatInvRot (sQuat sim) vAir
      rc = pilotToRc pilot
      (out, st') = stepFirmwareVehicleWithMassInertia
        massKg rollInertia True rc gameParams gameServo
        gameBattery kestrelModel step vAirB (sOmega sim) 1000 bodyUp (sFv sim)
      fB = totalForceN out
      mB = totalMomentNm out
      fW = quatRot (sQuat sim) fB
      vG' = addVec (sVGround sim) (scaleVec step (scaleVec (1/massKg) fW))
      vG'' = addVec vG' (scaleVec step (Vec3 0 0 (-9.81)))
      (txD, tyD, tzD) = airframeInertia kestrelModel massKg 0
      omega' = addVec (sOmega sim)
        (scaleVec step (Vec3 (x mB/txD) (y mB/tyD) (z mB/tzD)))
      q' = stepQuat (sQuat sim) omega' step
      sim' = Sim q' vG'' omega' st' (sT sim + step)
  in (sim', out)

f3 :: Double -> String
f3 v =
  let s = if v < 0 then "-" else ""
      a = abs v
      whole = truncate a :: Int
      frac = truncate ((a - fromIntegral whole) * 1000 + 0.5) :: Int
      fracS = replicate (3 - length (show frac)) '0' ++ show frac
  in s ++ show whole ++ "." ++ take 3 fracS

fmtVec :: Vec3 -> String
fmtVec (Vec3 a b c) = "(" ++ f3 a ++ "," ++ f3 b ++ "," ++ f3 c ++ ")"

-- | Run a scenario from arbitrary initial conditions, print state at 5 Hz.
runToss :: String -> Vec3 -> Vec3 -> Double -> [(Double, Double, Double, Double, Double)]
        -> IO ()
runToss label v0 om0 seconds script = do
  putStrLn $ "\n=== " ++ label ++ " v0=" ++ show v0 ++ " om0=" ++ show om0 ++ " ==="
  let sim0 = Sim (Quat 1 0 0 0) v0 om0
        (defaultFirmwareVehicleState { fvMassFrame = Just (0, iyD, izD) }) 0
      nSteps = round (seconds / dt) :: Int
  let loop :: Int -> Sim -> IO ()
      loop n sim | n <= 0 = pure ()
                 | otherwise = do
        let t = sT sim
            (_,thr,roll,pitch,yaw) = last [p | p@(s,_,_,_,_) <- script, s <= t]
            (sim', out) = stepSim dt (PilotInput thr roll pitch yaw 0.5 True) zeroVec sim
            bodyUp = quatInvRot (sQuat sim') (Vec3 0 0 1)
            bankDeg = asin (max (-1) (min 1 (y bodyUp))) * 180 / pi
            pitchDeg = asin (max (-1) (min 1 (negate (x bodyUp)))) * 180 / pi
            vAir = sVGround sim'
            vAirB = quatInvRot (sQuat sim') vAir
            betaDeg = atan2 (-(y vAirB)) (max 0.1 (x vAirB)) * 180 / pi
        if abs (t - fromIntegral (round (t*5)) / 5) < dt*0.6
          then putStrLn $ "t=" ++ f3 t
               ++ " pitch=" ++ f3 pitchDeg
               ++ " bank=" ++ f3 bankDeg
               ++ " yawR=" ++ f3 (z (sOmega sim'))
               ++ " beta=" ++ f3 betaDeg
               ++ " spd=" ++ f3 (magnitude vAirB)
               ++ " om=" ++ fmtVec (sOmega sim')
               ++ " M=" ++ fmtVec (totalMomentNm out)
               ++ " F=" ++ fmtVec (totalForceN out)
               ++ " flags=" ++ show (outputFlags out)
               ++ " flaps=" ++ f3 (servoAngleDeg (fvServoLeft (sFv sim')))
                 ++ "/" ++ f3 (servoAngleDeg (fvServoRight (sFv sim')))
          else pure ()
        loop (n-1) sim'
  loop nSteps sim0

main :: IO ()
main = do
  putStrLn ("airframeInertia (ix,iy,iz) = " ++ show (ixD, iyD, izD))
  -- 1. The user's real flight: toss reset state, neutral sticks, glide.
  runToss "TOSS-GLIDE-NEUTRAL" (Vec3 7 (-0.7) (-3.9)) (Vec3 (-0.19) (-1.13) (-0.16)) 10
    [(0,0,0,0,0)]
  -- 2. Level glide, roll-rate kick only.
  runToss "ROLL-KICK-GLIDE" (Vec3 8 0 0.3) (Vec3 1.5 0 0) 5
    [(0,0,0,0,0)]
  -- 3. Level glide, pitch-rate kick only.
  runToss "PITCH-KICK-GLIDE" (Vec3 8 0 0.3) (Vec3 0 1.0 0) 5
    [(0,0,0,0,0)]
  -- 4. Level glide, yaw-rate kick only.
  runToss "YAW-KICK-GLIDE" (Vec3 8 0 0.3) (Vec3 0 0 1.0) 5
    [(0,0,0,0,0)]
  -- 5. Toss state but with the roll stick hard over (the user's flight).
  runToss "TOSS-GLIDE-FULLROLL" (Vec3 7 (-0.7) (-3.9)) (Vec3 (-0.19) (-1.13) (-0.16)) 10
    [(0,0,1,0,0)]
  -- 6. THE USER'S 23:05 FLIGHT: level glide at throw speed, small roll
  --    stick (0.2) for 1 s, then release — watch whether the bank deepens
  --    with symmetric wings (the reported auto-rotation).
  runToss "GLIDE-ROLLPULSE-0.2" (Vec3 5.5 0 0.3) (Vec3 0 0 0) 8
    [(0,0,0,0,0),(1,0,0.2,0,0),(2,0,0,0,0)]
  -- 7. Same but full roll stick for 1 s.
  runToss "GLIDE-ROLLPULSE-1.0" (Vec3 5.5 0 0.3) (Vec3 0 0 0) 8
    [(0,0,0,0,0),(1,0,1.0,0,0),(2,0,0,0,0)]
  -- 8. THE USER'S TUMBLE STATE: top of the zoom climb, speed 3.9, path +26 deg,
  --    body level (AoA +26), neutral sticks — the 23:05 flight fell over here.
  runToss "ZOOM-STALL-NEUTRAL" (Vec3 3.5 0 1.7) (Vec3 0 0 0) 8
    [(0,0,0,0,0)]
  -- 9. Same state but with the small roll stick the user held.
  runToss "ZOOM-STALL-ROLL0.2" (Vec3 3.5 0 1.7) (Vec3 0 0 0) 8
    [(0,0,0.2,0,0)]