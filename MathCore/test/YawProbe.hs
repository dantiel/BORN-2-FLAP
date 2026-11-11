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
import qualified Born2Flap.Math.Firmware as FW
import Born2Flap.Math.Servo
  ( ServoSpec(..), BatterySpec(..), defaultServoSpec, defaultBatterySpec )
import Born2Flap.Math.Model ( kestrelModel )
import Born2Flap.Math.FirmwareVehicle
  ( FirmwareVehicleState(..), defaultFirmwareVehicleState
  , stepFirmwareVehicleWithMassInertia, TailField(..) )
import Born2Flap.Math.Servo ( servoAngleDeg )
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

-- | First-order orientation update: q' = normalize(q * (1, 0.5*dt*omega)).
stepQuat :: Quat -> Vec3 -> Double -> Quat
stepQuat q (Vec3 px py pz) dt =
  let half = 0.5 * dt
  in quatNorm (quatMul q (Quat 1 (half*px) (half*py) (half*pz)))

data Sim = Sim
  { sQuat    :: !Quat
  , sVGround :: !Vec3   -- ground-referenced velocity (m/s)
  , sOmega   :: !Vec3   -- body rates (rad/s)
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

-- | The live game's Kestrel tuning profile (Born2FlapTuning.h model 2).
gameParams :: FirmwareParams
gameParams =
  let base = defaultFirmwareParams
      prof = (fwProfile base)
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
  in base { fwServoSpeedMs = 80, fwFlapBaseFreqDh = 32, fwFlapCentreDeg = 0
          , fwVerticalMountIncidenceDeg = 0, fwProfile = prof }

gameServo :: ServoSpec
gameServo = defaultServoSpec
  { servoNoLoadSpeedDegPerSec = 750
  , servoStallTorqueNm = 1.332
  , servoBackdriveDegPerSecNm = 20
  }

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
      vAirB = quatInvRot (sQuat sim) vAir   -- body-frame, like the bridge
      rc = pilotToRc pilot
      (out, st') = stepFirmwareVehicleWithMassInertia
        massKg rollInertia True rc gameParams gameServo
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

-- Phase script: (tStart, throttle, roll, pitch, yaw) — flapping cruise.
-- FIXED-TAIL DOCTRINE. Scenario 1: the ROLL stick rotates the wings (centre
-- differential + timing skew): the bird banks, and the banked fixed tail
-- yaws the nose — bank AND yawRate must emerge together. Scenario 2: the
-- yaw stick commands NOTHING — wings stay symmetric, no bank, no yaw.
phasesAileron :: [(Double, Double, Double, Double, Double)]
phasesAileron = [ (0, 0.5, 0,    0, 0)
                , (8, 0.5, 0.40, 0, 0) ]

phasesRudder :: [(Double, Double, Double, Double, Double)]
phasesRudder = [ (0, 0.5, 0,    0, 0)
                , (8, 0.5, 0,    0, 0.4) ]

-- Glide yaw-stick: throttle 0 (glide branch) + right yaw stick. The tail is
-- FIXED: the yaw stick has no actuator anywhere — wings stay symmetric, no
-- bank, no yaw.
phasesGlideYaw :: [(Double, Double, Double, Double, Double)]
phasesGlideYaw = [ (0, 0, 0, 0, 0.5) ]

-- Glide roll-stick: throttle 0 (glide branch) + right roll stick. The wings
-- hold differential incidence, the bird banks, and the banked FIXED tail
-- yaws the nose — the pure aero chain, no moving tail.
phasesGlideRoll :: [(Double, Double, Double, Double, Double)]
phasesGlideRoll = [ (0, 0, 0.5, 0, 0) ]

-- Stability checks: neutral sticks must NOT auto-rotate (no bank/yaw drift);
-- a SMALL aileron must produce a bounded bank with proverse yaw — the feel
-- the user demands (turn by the banked fixed tail, not by fin weathervane).
phasesNeutralGlide :: [(Double, Double, Double, Double, Double)]
phasesNeutralGlide = [ (0, 0, 0, 0, 0) ]

-- Full-stick roll: the overdrive condition. The bank must build, yaw must
-- emerge from the tilted tail, and the turn must settle — no barrel roll.
phasesFullRollGlide :: [(Double, Double, Double, Double, Double)]
phasesFullRollGlide = [ (0, 0, 0, 0, 0), (6, 0, 1.0, 0, 0) ]

phasesFullRollFlap :: [(Double, Double, Double, Double, Double)]
phasesFullRollFlap = [ (0, 0.5, 0, 0, 0), (8, 0.5, 1.0, 0, 0) ]

-- Strong lateral gust with neutral sticks: a wind field must only push the
-- bird (inertia), never steer it. No auto-bank, no auto-yaw.
phasesNeutralGust :: [(Double, Double, Double, Double, Double)]
phasesNeutralGust = [ (0, 0.5, 0, 0, 0) ]

phasesSmallAileronGlide :: [(Double, Double, Double, Double, Double)]
phasesSmallAileronGlide = [ (0, 0, 0, 0, 0), (6, 0, 0.15, 0, 0) ]

phasesNeutralFlap :: [(Double, Double, Double, Double, Double)]
phasesNeutralFlap = [ (0, 0.5, 0, 0, 0) ]

phasesSmallAileronFlap :: [(Double, Double, Double, Double, Double)]
phasesSmallAileronFlap = [ (0, 0.5, 0, 0, 0), (8, 0.5, 0.15, 0, 0) ]

pilotAt :: [(Double, Double, Double, Double, Double)] -> Double -> PilotInput
pilotAt script t =
  let (_,thr,roll,pitch,yaw) = last [p | p@(s,_,_,_,_) <- script, s <= t]
  in PilotInput thr roll pitch yaw 0.5 True

tailFieldForce :: TailField -> Vec3
tailFieldForce f = addVec (tfLift f) (addVec (tfPressure f) (tfDrag f))

-- | Format a double with fixed decimals.
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

runScenario :: String -> Vec3 -> Double -> IO ()
runScenario label wind seconds =
  runScenarioAt label wind seconds dt phasesAileron

runScenarioAt :: String -> Vec3 -> Double -> Double
              -> [(Double, Double, Double, Double, Double)] -> IO ()
runScenarioAt label wind seconds step script = do
  putStrLn $ "\n=== " ++ label ++ " (wind " ++ show wind ++ ", dt=" ++ f3 step ++ ") ==="
  let sim0 = Sim (Quat 1 0 0 0) (Vec3 8 0 0.3) zeroVec
        (defaultFirmwareVehicleState { fvMassFrame = Just (0, iyD, izD) }) 0
      nSteps = round (seconds / step) :: Int
  let loop :: Int -> Sim -> IO ()
      loop n sim | n <= 0 = pure ()
                 | otherwise = do
        let t = sT sim
            (sim', out) = stepSim step (pilotAt script t) wind sim
            bodyUp = quatInvRot (sQuat sim') (Vec3 0 0 1)
            bankDeg = asin (max (-1) (min 1 (y bodyUp))) * 180 / pi
            worldRates = quatRot (sQuat sim') (sOmega sim')
            vAir = addVec (sVGround sim') (scaleVec (-1) wind)
            vAirB = quatInvRot (sQuat sim') vAir
            betaDeg = atan2 (-(y vAirB)) (max 0.1 (x vAirB)) * 180 / pi
        if abs (t - fromIntegral (round (t*5)) / 5) < step*0.6
          then let vIn = addVec (sVGround sim) (scaleVec (-1) wind)
                   vInB = quatInvRot (sQuat sim) vIn
                   tfPos = case fvTailFields (sFv sim') of
                     (tf:_) -> tfPosition tf; [] -> zeroVec
                   tfVel = case fvTailFields (sFv sim') of
                     (tf:_) -> tfVelocity tf; [] -> zeroVec
                   dvImplied = addVec tfVel (scaleVec (-1)
                     (addVec vInB (crossVec (sOmega sim') tfPos)))
               in putStrLn $ "t=" ++ f3 t
               ++ " bank=" ++ f3 bankDeg
               ++ " yawRate=" ++ f3 (z worldRates)
               ++ " beta=" ++ f3 betaDeg
               ++ " airspd=" ++ f3 (magnitude vAirB)
               ++ " vIn=" ++ fmtVec vInB
               ++ " dv=" ++ fmtVec dvImplied
               ++ " vB=" ++ fmtVec vAirB
               ++ " F=" ++ fmtVec (totalForceN out)
               ++ " mDv=" ++ fmtVec (scaleVec (massKg/dt) dvImplied)
               ++ " flags=" ++ show (outputFlags out)
               ++ " M=" ++ fmtVec (totalMomentNm out)
               ++ " ext=" ++ fmtVec (fvExternalForce (sFv sim'))
               ++ " tail=" ++ fmtVec (foldl' addVec zeroVec
                     (map tailFieldForce (fvTailFields (sFv sim'))))
               ++ " tailV=" ++ fmtVec (case fvTailFields (sFv sim') of
                     (tf:_) -> tfVelocity tf; [] -> zeroVec)
               ++ " flaps=" ++ f3 (servoAngleDeg (fvServoLeft (sFv sim')))
                     ++ "/" ++ f3 (servoAngleDeg (fvServoRight (sFv sim')))
          else pure ()
        loop (n-1) sim'
  loop nSteps sim0

main :: IO ()
main = do
  putStrLn ("airframeInertia (ix,iy,iz) = " ++ show (ixD, iyD, izD))
  runScenario "AILERON-NO-WIND" zeroVec 16
  runScenario "AILERON-CROSSWIND +Y 3m/s" (Vec3 0 3 0) 16
  runScenarioAt "RUDDER-NO-WIND" zeroVec 16 dt phasesRudder
  runScenarioAt "GLIDE-YAW-STICK" zeroVec 12 dt phasesGlideYaw
  runScenarioAt "GLIDE-ROLL-STICK" zeroVec 12 dt phasesGlideRoll
  runScenarioAt "GLIDE-NEUTRAL-STABILITY" zeroVec 20 dt phasesNeutralGlide
  runScenarioAt "GLIDE-SMALL-AILERON" zeroVec 20 dt phasesSmallAileronGlide
  runScenarioAt "FLAP-NEUTRAL-STABILITY" zeroVec 16 dt phasesNeutralFlap
  runScenarioAt "FLAP-SMALL-AILERON" zeroVec 16 dt phasesSmallAileronFlap
  runScenarioAt "GLIDE-FULL-ROLL" zeroVec 20 dt phasesFullRollGlide
  runScenarioAt "FLAP-FULL-ROLL" zeroVec 20 dt phasesFullRollFlap
  runScenarioAt "FLAP-NEUTRAL-GUST +Y 6m/s" (Vec3 0 6 0) 16 dt phasesNeutralGust