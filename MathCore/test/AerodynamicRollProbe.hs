{-# LANGUAGE BangPatterns #-}
{-# LANGUAGE DerivingStrategies #-}
{-# LANGUAGE OverloadedStrings #-}
-- AerodynamicRollProbe
--
-- Measures the REAL aerodynamic forces/moments on both wings and the body
-- for a differential flap (left wing up +delta, right wing down -delta) at
-- prescribed hinge rates, in two operating conditions:
--   A/B: steady forward glide at 8 m/s   (settled, strip-level model)
--   C:   hover (V=0)                     (settled, strip-level model)
--   D:   added-mass partition            (SpatialDynamics wet vs dry)
--   E:   closed-loop FLAPPING flight     (firmware + servo + spatial solve)
--   F:   closed-loop slow differential   (does the body follow the wings?)
--
-- Sign conventions (from the live model):
--   * servo/flap angle positive = wing raised on its own side.
--   * strip hinge coordinate u = side*stroke is a right-hand rotation about
--     +X (body axes: +X forward, +Y right, +Z up): for the LEFT wing u<0
--     raises it, for the RIGHT wing u>0 raises it.
--   * hingeTorque side returns the generalized torque CONJUGATE to u
--     (power = Q_code * u'), i.e. the aerodynamic load the servo opposes.
--   * Q_code is conjugate to the STROKE coordinate (positive stroke raises
--     the wing on its own side = the servo angle).  The task's differential
--     coordinate (delta_L = +delta left up, delta_R = -delta right down)
--     has Q_delta = Q_code_L - Q_code_R.
--   * Virtual work: P + QL*strokeRateL + QR*strokeRateR = 0 (VW column).
module Main (main) where

import Data.List (foldl')
import System.IO (hSetEncoding, stdout, utf8)
import Text.Printf (printf)

import Born2Flap.Math.Types ( Vec3(..) )
import Born2Flap.Math.Vehicle
  ( VehicleInput(..), VehicleOutput(..), StripState(..), StripResult(..)
  , stripCount, initialStrips, prepareWingLoads, stepCoupledWing
  , airDensity, wingRootHeight, zeroVec, addVec, scaleVec, crossVec, magnitude
  , radians )
import Born2Flap.Math.FirmwareVehicle
  ( FirmwareVehicleState(..), defaultFirmwareVehicleState
  , stepFirmwareVehicleWithMassInertia, hingeTorque )
import Born2Flap.Math.Firmware
  ( PilotInput(..), pilotToRc, defaultFirmwareParams, FirmwareParams(..)
  , FlightProfile(..) )
import Born2Flap.Math.Servo
  ( ServoSpec(..), BatterySpec(..), defaultServoSpec, defaultBatterySpec
  , servoAngleDeg )
import Born2Flap.Math.Model ( kestrelModel, WingModel(..) )
import Born2Flap.Math.Airframe ( airframeInertia )
import Born2Flap.Math.Planform
  ( shapeStationsAt, wsSpanM, wsShoulderM, SpanStation(..) )
import Born2Flap.Math.Section ( zeroLiftAngle )
import qualified Born2Flap.Math.SpatialDynamics as Spatial

-- ---------------------------------------------------------------------------
-- Formatting / vector helpers
-- ---------------------------------------------------------------------------

f3 :: Double -> String
f3 = printf "%.6f"

deg :: Double -> Double
deg r = r * 180 / pi

dot :: Vec3 -> Vec3 -> Double
dot (Vec3 a b c) (Vec3 d e f) = a*d + b*e + c*f

massKg :: Double
massKg = 0.050

(ixD, iyD, izD) = airframeInertia kestrelModel massKg 0

dt :: Double
dt = 1.0 / 240.0

stations :: [SpanStation]
stations = shapeStationsAt (wmWing kestrelModel) stripCount

chordAt :: Int -> Double
chordAt i = spanChord (stations !! i)

clampD :: Double -> Double -> Double -> Double
clampD lo hi = max lo . min hi

-- ---------------------------------------------------------------------------
-- Strip-level evaluation (pure prescribed kinematics, elastic DOFs solved)
-- ---------------------------------------------------------------------------

aeroInput :: Double -> VehicleInput
aeroInput velocity = VehicleInput dt (Vec3 velocity 0 0) zeroVec 0 0 0 0 1000 (Vec3 0 0 1)

-- Settle strip memory (separation, camber, elastic state) at the condition.
-- incidenceRad = mount/body pitch incidence applied to every strip.
settleWing :: Double -> Double -> Double -> Double -> [StripState]
settleWing side stroke incidenceRad velocity =
  let input = aeroInput velocity
      n = round (2.0 / dt) :: Int
      go k s | k <= 0 = s
             | otherwise =
                 go (k - 1) (snd (stepCoupledWing False side stroke 0 0 0 input kestrelModel incidenceRad s))
  in go n initialStrips

-- Evaluate strip loads at the prescribed stroke / hinge rate / body roll rate.
evaluateWing :: Double -> Double -> Double -> Double -> Double -> Double -> [StripState] -> [StripResult]
evaluateWing side stroke incidenceRad strokeRate rollRate velocity states =
  let input = aeroInput velocity
      loads = prepareWingLoads False side stroke 0 input kestrelModel incidenceRad states
  in loads rollRate strokeRate 0

-- Generalized hinge torque contribution of a force component, conjugate to
-- the hinge coordinate u = side*stroke (right-hand about +X at height h).
qOfForce :: Double -> Vec3 -> [StripResult] -> Double
qOfForce side f rs =
  side * sum [ x (crossVec (stripPosition st) f) + wingRootHeight * y f
             | r <- rs, let st = resultState r ]

data WingMetrics = WingMetrics
  { wmQ      :: !Double    -- hinge generalized torque, conjugate to u
  , wmLift   :: !Vec3      -- circulation lift (attached + rotational + LEV)
  , wmPress  :: !Vec3      -- normal pressure (residual of lift+drag vs force)
  , wmDrag   :: !Vec3      -- skin + induced + spanwise drag
  , wmForce  :: !Vec3      -- total aero force
  , wmMoment :: !Vec3      -- total aero moment about the body origin
  , wmPower  :: !Double    -- signed mechanical power (wing on air)
  , wmQLift  :: !Double
  , wmQDrag  :: !Double
  , wmQPress :: !Double
  , wmQCouple :: !Double   -- section pitch couple part of the hinge torque
  , wmQRot   :: !Double    -- estimated rotationalCl (Kramer) contribution
  , wmAlpha  :: [(Double, Double, Double, Double)] -- alpha[deg], alphaEff[deg], rotCl, q[Pa]
  }

wingMetrics :: Double -> Double -> Double -> [StripResult] -> WingMetrics
wingMetrics side strokeRate rollRate rs =
  let sts = map resultState rs
      q = hingeTorque side rs
      lift = foldl' addVec zeroVec (map stripLift sts)
      press = foldl' addVec zeroVec (map stripPressure sts)
      drag = foldl' addVec zeroVec (map stripDrag sts)
      force = foldl' addVec zeroVec (map resultForce rs)
      moment = foldl' addVec zeroVec (map resultMoment rs)
      power = sum (map resultPower rs)
      -- Per-strip component hinge contributions: each strip's own component
      -- force at its own position (generalized torque conjugate to u).
      qOfField f = side * sum
        [ x (crossVec (stripPosition (resultState r)) (f r)) + wingRootHeight * y (f r) | r <- rs ]
      qLift = qOfField (stripLift . resultState)
      qDrag = qOfField (stripDrag . resultState)
      qPress = qOfField (stripPressure . resultState)
      qCouple = side * sum [ x (addVec (resultMoment r) (scaleVec (-1) (crossVec (stripPosition (resultState r)) (resultForce r)))) | r <- rs ]
      -- Kramer rotationalCl estimate, reconstructed from the stored states.
      alphas = [ alphaInfo i r | (i, r) <- zip [0..] rs ]
      alphaInfo i r =
        let st = resultState r
            (_, chordAxis, spanAxis, normal, flow, _) = resultFrame r
            chordVel = dot flow chordAxis
            normalVel = dot flow normal
            planarSpeed = sqrt (chordVel*chordVel + normalVel*normalVel)
            safe = max 1.0e-6 planarSpeed
            alpha = atan2 (-normalVel) chordVel
            alphaEff = alpha - zeroLiftAngle (stripCamber st)
            sectionPitchRate = stripTwistRateRadS st - side * dot
              (addVec (Vec3 rollRate 0 0) (Vec3 (side*strokeRate) 0 0)) spanAxis
            rotCl = clampD (-0.65) 0.65 (1.0 * spanChord (stations !! i) * sectionPitchRate / safe)
            qDyn = 0.5 * airDensity * planarSpeed * planarSpeed
        in (deg alpha, deg alphaEff, rotCl, qDyn)
      -- rotational force share = rotCl/cl of the circulation lift
      qRot = side * sum
        [ let st = resultState r
              (_, chordAxis, spanAxis, normal, flow, _) = resultFrame r
              chordVel = dot flow chordAxis
              normalVel = dot flow normal
              planarSpeed = sqrt (chordVel*chordVel + normalVel*normalVel)
              area = spanChord (stations !! i) * (wsSpanM (wmWing kestrelModel) / fromIntegral stripCount)
              qDyn = 0.5 * airDensity * planarSpeed * planarSpeed
              cl = magnitude (stripLift st) / max 1e-9 (qDyn * area)
              sectionPitchRate = stripTwistRateRadS st - side * dot
                (addVec (Vec3 rollRate 0 0) (Vec3 (side*strokeRate) 0 0)) spanAxis
              rotCl = clampD (-0.65) 0.65 (1.0 * spanChord (stations !! i) * sectionPitchRate / max 1.0e-6 planarSpeed)
              fRot = scaleVec (rotCl / max 1e-9 cl) (stripLift st)
          in x (crossVec (stripPosition st) fRot) + wingRootHeight * y fRot
        | (i, r) <- zip [0..] rs ]
  in WingMetrics q lift press drag force moment power qLift qDrag qPress qCouple qRot alphas

-- ---------------------------------------------------------------------------
-- Condition runner: settled state per delta, then all rate trials
-- ---------------------------------------------------------------------------

-- Evaluate a whole differential condition. deltaDeg>0: left up / right down.
-- hingeRateDegS>0: left rising / right falling. rollRateDegS: body roll rate.
condEval :: String -> Double -> Double -> Double -> Double -> IO ()
condEval label deltaDeg hingeRateDegS rollRateDegS velocity = do
    let delta = radians deltaDeg
        rate = radians hingeRateDegS
        rollRate = radians rollRateDegS
        -- Body pitch attitude: +6 deg incidence so the glide carries ~weight.
        incidence = if velocity > 0.5 then radians 3 else 0
        rsL = evaluateWing (-1) delta incidence rate rollRate velocity (settleWing (-1) delta incidence velocity)
        rsR = evaluateWing 1 (-delta) incidence (-rate) rollRate velocity (settleWing 1 (-delta) incidence velocity)
        mL = wingMetrics (-1) rate rollRate rsL
        mR = wingMetrics 1 (-rate) rollRate rsR
        qL = wmQ mL
        qR = wmQ mR
        -- Q_code is conjugate to the STROKE coordinate (positive stroke raises
        -- the wing on its own side = the servo angle).  The differential
        -- coordinate of the task (delta_L=+d left up, delta_R=-d right down)
        -- has Q_delta = QL - QR with QL,QR the stroke-conjugate torques.
        qDelta = qL - qR
        mx = x (addVec (wmMoment mL) (wmMoment mR))
        fy = y (addVec (wmForce mL) (wmForce mR))
        mz = z (addVec (wmMoment mL) (wmMoment mR))
        liftZ = z (addVec (wmLift mL) (wmLift mR))
        power = wmPower mL + wmPower mR
        -- (strokeRateL = +rate, strokeRateR = -rate).
        vw = power + qL * rate + qR * (-rate)
        qdPress = wmQPress mL - wmQPress mR
        qdLift = wmQLift mL - wmQLift mR
        qdDrag = wmQDrag mL - wmQDrag mR
        qdCouple = wmQCouple mL - wmQCouple mR
        qdRot = wmQRot mL - wmQRot mR
    putStrLn $ "  " ++ label
      ++ " QL=" ++ f3 qL ++ " QR=" ++ f3 qR ++ " Qd=" ++ f3 qDelta
      ++ " Mx=" ++ f3 mx ++ " Fy=" ++ f3 fy ++ " Mz=" ++ f3 mz
      ++ " Lz=" ++ f3 liftZ ++ " P=" ++ f3 power ++ " VW=" ++ f3 vw
      ++ " | Qd parts: press=" ++ f3 qdPress ++ " lift=" ++ f3 qdLift
      ++ " drag=" ++ f3 qdDrag ++ " couple=" ++ f3 qdCouple ++ " rot=" ++ f3 qdRot
    pure ()

-- Per-wing detail for a single condition (Part A).
wingDetail :: Double -> Double -> Double -> IO ()
wingDetail deltaDeg hingeRateDegS velocity = do
  let delta = radians deltaDeg
      rate = radians hingeRateDegS
      incidence = if velocity > 0.5 then radians 3 else 0
      mL = wingMetrics (-1) rate 0 (evaluateWing (-1) delta incidence rate 0 velocity (settleWing (-1) delta incidence velocity))
      mR = wingMetrics 1 (-rate) 0 (evaluateWing 1 (-delta) incidence (-rate) 0 velocity (settleWing 1 (-delta) incidence velocity))
      dump name m =
        putStrLn $ "    " ++ name
          ++ " Q=" ++ f3 (wmQ m)
          ++ " lift=" ++ f3 (magnitude (wmLift m)) ++ "(" ++ f3 (x (wmLift m)) ++ "," ++ f3 (y (wmLift m)) ++ "," ++ f3 (z (wmLift m)) ++ ")"
          ++ " press=" ++ f3 (magnitude (wmPress m))
          ++ " drag=" ++ f3 (magnitude (wmDrag m))
          ++ " F=(" ++ f3 (x (wmForce m)) ++ "," ++ f3 (y (wmForce m)) ++ "," ++ f3 (z (wmForce m)) ++ ")"
          ++ " P=" ++ f3 (wmPower m)
          ++ " aEff(root/mid/tip)="
          ++ f3 (head [b | (_,b,_,_) <- take 1 (wmAlpha m)]) ++ "/"
          ++ f3 (head [b | (_,b,_,_) <- take 1 (drop 7 (wmAlpha m))]) ++ "/"
          ++ f3 (head [b | (_,b,_,_) <- take 1 (drop 15 (wmAlpha m))])
          ++ " rotCl(root/mid/tip)="
          ++ f3 (head [c | (_,_,c,_) <- take 1 (wmAlpha m)]) ++ "/"
          ++ f3 (head [c | (_,_,c,_) <- take 1 (drop 7 (wmAlpha m))]) ++ "/"
          ++ f3 (head [c | (_,_,c,_) <- take 1 (drop 15 (wmAlpha m))])
  dump "L" mL
  dump "R" mR

-- ---------------------------------------------------------------------------
-- Part D: added-mass partition from SpatialDynamics
-- ---------------------------------------------------------------------------

partAddedMass :: IO ()
partAddedMass = do
  putStrLn "\n=== PART D: ADDED-MASS GENERALIZED INERTIA (wet - dry) ==="
  putStrLn "  (M entries: kg.m^2; bias rows: generalized force at constant rate)"
  let deltas = [0, 5, 10, 20, 30] :: [Double]
      rates = [0.1, 1, 5, 20, 100] :: [Double]
      velocity = 8.0
      goDelta d =
        let dl = radians d
            mk rho =
              let v8 = [velocity, 0, 0, 0, 0, 0, radians (d*0), radians (d*0)]
                  (m, bias, _) = Spatial.spatialSystem kestrelModel massKg 0
                    (ixD, iyD, izD) rho (dl, -dl) v8 zeroVec
              in (m, bias)
            (wetM, wetB) = mk airDensity
            (dryM, dryB) = mk 0
            dM i j = wetM !! i !! j - dryM !! i !! j
            dB i = wetB !! i - dryB !! i
        in putStrLn $ "  d=" ++ f3 d ++ "deg: dM33=" ++ f3 (dM 3 3)
             ++ " dM36=" ++ f3 (dM 3 6) ++ " dM37=" ++ f3 (dM 3 7)
             ++ " dM66=" ++ f3 (dM 6 6) ++ " dM77=" ++ f3 (dM 7 7)
             ++ " dM67=" ++ f3 (dM 6 7)
             ++ " | dB(3,6,7)=" ++ f3 (dB 3) ++ "," ++ f3 (dB 6) ++ "," ++ f3 (dB 7)
             ++ " (at rest)"
      goRate r =
        let dl = radians 10
            v8 = [velocity, 0, 0, 0, 0, 0, radians r, -radians r]
            mk rho =
              let (m, bias, _) = Spatial.spatialSystem kestrelModel massKg 0
                    (ixD, iyD, izD) rho (dl, -dl) v8 zeroVec
              in (m, bias)
            (wetM, wetB) = mk airDensity
            (dryM, dryB) = mk 0
            dB i = wetB !! i - dryB !! i
            dM i j = wetM !! i !! j - dryM !! i !! j
            accel = [0,0,0,0,0,0,1,1]  -- 1 rad/s^2 on both hinge coordinates
            -- added-mass generalized force at this rate plus 1 rad/s^2 joint accel:
            jointForce i = dB i + sum [ dM i j * accel !! j | j <- [0..7] ]
        in putStrLn $ "  rate=" ++ f3 r ++ "deg/s (d=10deg): dB hinge L/R="
             ++ f3 (dB 6) ++ "/" ++ f3 (dB 7) ++ " roll=" ++ f3 (dB 3)
             ++ " | added-mass hinge force @1rad/s^2: L/R="
             ++ f3 (jointForce 6) ++ "/" ++ f3 (jointForce 7)
  mapM_ goDelta deltas
  mapM_ goRate rates

-- ---------------------------------------------------------------------------
-- Parts A/B/C: glide + hover static and rate sweeps
-- ---------------------------------------------------------------------------

partStatic :: String -> Double -> IO ()
partStatic name velocity = do
  putStrLn $ "\n=== PART " ++ name ++ ": STATIC DIFFERENTIAL vs ANGLE (V="
    ++ f3 velocity ++ " m/s, hinge rate 0) ==="
  mapM_ (\d -> condEval ("d=" ++ f3 d ++ "deg") d 0 0 velocity) [0, 5, 10, 20, 30]
  putStrLn "\n  Wing detail (per wing, d=20deg, rate 0):"
  wingDetail 20 0 velocity
  putStrLn $ "\n=== PART " ++ name ++ ": DIFFERENTIAL HINGE RATE SWEEP (d=10deg) ==="
  mapM_ (\r -> condEval ("rate=" ++ f3 r ++ "deg/s") 10 r 0 velocity) [0.1, 1, 5, 20, 100]
  putStrLn $ "\n=== PART " ++ name ++ ": RATE SWEEP AT d=0deg (symmetric) ==="
  mapM_ (\r -> condEval ("rate=" ++ f3 r ++ "deg/s") 0 r 0 velocity) [0.1, 1, 5, 20, 100]
  putStrLn $ "\n=== PART " ++ name ++ ": RATE SWEEP AT d=30deg (strong differential) ==="
  mapM_ (\r -> condEval ("rate=" ++ f3 r ++ "deg/s") 30 r 0 velocity) [0.1, 1, 5, 20, 100]
  putStrLn $ "\n=== PART " ++ name ++ ": BODY ROLL RATE SWEEP (d=10deg, hinge rate 0) ==="
  mapM_ (\p -> condEval ("bodyP=" ++ f3 p ++ "deg/s") 10 0 p velocity) [0.1, 1, 5, 20]

-- ---------------------------------------------------------------------------
-- Parts E/F: closed-loop flapping + slow differential
-- ---------------------------------------------------------------------------

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

data Quat = Quat !Double !Double !Double !Double

quatNorm :: Quat -> Quat
quatNorm (Quat a b c d) =
  let n = sqrt (a * a + b * b + c * c + d * d)
  in Quat (a / n) (b / n) (c / n) (d / n)

quatMul :: Quat -> Quat -> Quat
quatMul (Quat a1 b1 c1 d1) (Quat a2 b2 c2 d2) = Quat
  (a1 * a2 - b1 * b2 - c1 * c2 - d1 * d2)
  (a1 * b2 + b1 * a2 + c1 * d2 - d1 * c2)
  (a1 * c2 - b1 * d2 + c1 * a2 + d1 * b2)
  (a1 * d2 + b1 * c2 - c1 * b2 + d1 * a2)

quatInvRot :: Quat -> Vec3 -> Vec3
quatInvRot (Quat a b c d) (Vec3 vx vy vz) =
  let t2 = a * b; t3 = a * c; t4 = a * d; t5 = -b * b; t6 = b * c; t7 = b * d
      t8 = -c * c; t9 = c * d; t10 = -d * d
      wx = 2 * ((t8 + t10) * vx + (t6 + t4) * vy + (t3 - t7) * vz) + vx
      wy = 2 * ((t4 - t6) * vx + (t5 + t10) * vy + (t9 + t2) * vz) + vy
      wz = 2 * ((t7 + t3) * vx + (t2 - t9) * vy + (t5 + t8) * vz) + vz
  in Vec3 wx wy wz

quatRot :: Quat -> Vec3 -> Vec3
quatRot (Quat a b c d) v = quatInvRot (Quat a (-b) (-c) (-d)) v

stepQuat :: Quat -> Vec3 -> Double -> Quat
stepQuat q (Vec3 px py pz) dtq =
  let half = 0.5 * dtq
  in quatNorm (quatMul q (Quat 1 (half * px) (half * py) (half * pz)))

data Sim = Sim
  { sQuat    :: !Quat
  , sVGround :: !Vec3
  , sOmega   :: !Vec3
  , sFv      :: !FirmwareVehicleState
  , sT       :: !Double
  }

stepSim :: Double -> PilotInput -> Sim -> (Sim, VehicleOutput)
stepSim step pilot sim =
  let bodyUp = quatInvRot (sQuat sim) (Vec3 0 0 1)
      vAirB = quatInvRot (sQuat sim) (sVGround sim)
      rc = pilotToRc pilot
      (out, st') = stepFirmwareVehicleWithMassInertia
        massKg ixD True rc gameParams gameServo
        gameBattery kestrelModel step vAirB (sOmega sim) 1000 bodyUp (sFv sim)
      fB = totalForceN out
      mB = totalMomentNm out
      fW = quatRot (sQuat sim) fB
      vG' = addVec (sVGround sim)
              (scaleVec step (addVec (scaleVec (1 / massKg) fW) (Vec3 0 0 (-9.81))))
      mOver = Vec3 (x mB / ixD) (y mB / iyD) (z mB / izD)
      omega' = addVec (sOmega sim) (scaleVec step mOver)
      q' = stepQuat (sQuat sim) omega' step
  in ( Sim { sQuat = q', sVGround = vG', sOmega = omega', sFv = st', sT = sT sim + step }
     , out )

-- Run the closed loop; feed a per-cycle callback with the step samples.
runClosed :: String -> Double -> Double -> Double -> Double
          -> ([Sim] -> IO ()) -> IO (Maybe Sim)
runClosed label throttle0 stickRamp stickHold seconds perCycle = do
  putStrLn $ "\n=== " ++ label ++ " ==="
  let sim0 = Sim (Quat 1 0 0 0) (Vec3 8 0 0.3) zeroVec
        (defaultFirmwareVehicleState { fvMassFrame = Just (0, iyD, izD) }) 0
      nSteps = round (seconds / dt) :: Int
      rollAt t = if t < 1 then 0 else min 1 (t / max 0.01 stickRamp) * stickHold
      loop :: Int -> Sim -> [Sim] -> IO (Maybe Sim)
      loop n sim acc | n <= 0 = pure (Just sim)
                     | otherwise =
        let t = sT sim
            pilot = PilotInput throttle0 (rollAt t) 0 0 0.5 True
            (sim', out) = stepSim dt pilot sim
            flags = outputFlags out
        in if flags /= 0 || not (allFinite sim')
             then do
               putStrLn ("  ABORT: flags=" ++ show flags ++ " at t=" ++ f3 t)
               pure Nothing
             else
               let aL = servoAngleDeg (fvServoLeft (sFv sim'))
                   aR = servoAngleDeg (fvServoRight (sFv sim'))
                   common = (aL + aR) / 2
                   prev = case acc of [] -> 0; (p:_) -> servoAngleDeg (fvServoLeft (sFv p)) + servoAngleDeg (fvServoRight (sFv p))
                   crossed = common >= 0 && prev / 2 < 0
                   acc' = sim' : acc
               in do
                 if crossed && length acc' > 100
                   then do perCycle acc'; loop (n - 1) sim' []
                   else loop (n - 1) sim' acc'
  loop nSteps sim0 []
  where
    allFinite sim' =
      let v = sVGround sim'
          o = sOmega sim'
      in not (isNaN (x v) || isInfinite (x v) || isNaN (y v) || isInfinite (y v)
              || isNaN (z v) || isInfinite (z v) || isNaN (x o) || isInfinite (x o))

cycleSummary :: [Sim] -> String
cycleSummary samples =
  let n = length samples
      mean f = sum [f s | s <- samples] / fromIntegral n
      aL = mean (servoAngleDeg . fvServoLeft . sFv)
      aR = mean (servoAngleDeg . fvServoRight . sFv)
      hL = mean (fvLeftHingeTorqueNm . sFv)
      hR = mean (fvRightHingeTorqueNm . sFv)
      hang = mean (fvHangDelta . sFv)
      pMean = mean (x . sOmega)
      bank = mean (\s -> let up = quatInvRot (sQuat s) (Vec3 0 0 1) in atan2 (y up) (z up) * 180 / pi)
  in "  cycle: n=" ++ show n
     ++ " flapL/R=" ++ f3 aL ++ "/" ++ f3 aR
     ++ " diff=" ++ f3 ((aL - aR) / 2)
     ++ " hingeL/R=" ++ f3 hL ++ "/" ++ f3 hR
     ++ " hang=" ++ f3 hang
     ++ " bank=" ++ f3 bank
     ++ " p=" ++ f3 pMean

partFlapping :: IO ()
partFlapping = do
  putStrLn "\n=== PART E: CLOSED-LOOP FLAPPING (throttle 0.6), mean over full cycles ==="
  putStrLn "  baseline (no differential):"
  _ <- runClosed "E1 baseline" 0.6 1 0 6
    (\s -> putStrLn (cycleSummary s))
  putStrLn "  static differential (roll stick 0.25 held):"
  _ <- runClosed "E2 differential" 0.6 1 0.25 8
    (\s -> putStrLn (cycleSummary s))
  putStrLn "  slow differential ramp (stick 0 -> 0.25 over 4 s):"
  _ <- runClosed "E3 slow ramp" 0.6 4 0.25 6
    (\s -> putStrLn (cycleSummary s))
  pure ()

-- ---------------------------------------------------------------------------
-- Part E': prescribed flapping, cycle-averaged strip loads (task condition 2)
-- ---------------------------------------------------------------------------

flapAmpDeg, flapFreqHz :: Double
flapAmpDeg = 20
flapFreqHz = 2.5

-- Prescribed symmetric flapping plus differential, averaged over complete
-- flap cycles at the strip level (no servo/firmware in the loop, so the
-- mean aerodynamic hinge loads and body moments of the REAL flapping state
-- are isolated from actuator dynamics).
partFlappingPrescribed :: IO ()
partFlappingPrescribed = do
  putStrLn "\n=== PART E': PRESCRIBED FLAPPING, MEAN OVER FULL FLAP CYCLES ==="
  let amp = radians flapAmpDeg
      omega = 2 * pi * flapFreqHz
      phaseCount = 48 :: Int
      cycles = 4 :: Int
      stkOf side delta t = amp * sin (omega * t) + (if side < 0 then delta else -delta)
      rteOf side delta deltaRate t = amp * omega * cos (omega * t)
        + (if side < 0 then deltaRate else -deltaRate)
      settleFlap velocity incidence side deltaDeg =
        let delta = radians deltaDeg
            input = aeroInput velocity
            n = cycles * phaseCount
            go k t s | k <= 0 = s
                     | otherwise =
                         let t' = t + dt
                             st' = snd (stepCoupledWing True side (stkOf side delta t') 0
                                        (rteOf side delta 0 t') (rteOf side delta 0 t')
                                        input kestrelModel incidence s)
                         in go (k - 1) t' st'
        in go n 0 initialStrips
      average velocity incidence side deltaDeg deltaRateDegS states =
        let delta = radians deltaDeg
            deltaRate = radians deltaRateDegS
            samples = [ (t, evaluateWing side (stkOf side delta t) incidence
                           (rteOf side delta deltaRate t) 0 velocity states)
                      | k <- [0 .. cycles * phaseCount - 1]
                      , let t = fromIntegral k / (flapFreqHz * fromIntegral phaseCount) ]
            mean f = sum [ f rs | (_, rs) <- samples ] / fromIntegral (length samples)
            mQ = mean (hingeTorque side)
            mF = scaleVec (1 / fromIntegral (length samples))
                   (foldl' addVec zeroVec [ foldl' addVec zeroVec (map resultForce rs) | (_, rs) <- samples ])
            mM = scaleVec (1 / fromIntegral (length samples))
                   (foldl' addVec zeroVec [ foldl' addVec zeroVec (map resultMoment rs) | (_, rs) <- samples ])
            mLz = mean (z . foldl' addVec zeroVec . map (stripLift . resultState))
            mP = mean (sum . map resultPower)
            -- Per-sample virtual-work residual P + Q*strokeRate (the FULL
            -- stroke rate, flap + differential), averaged over the cycle.
            mVW = sum [ sum (map resultPower rs) + hingeTorque side rs * rteOf side delta deltaRate t
                      | (t, rs) <- samples ] / fromIntegral (length samples)
        in (mQ, mF, mM, mLz, mP, mVW)
      row velocity incidence label deltaDeg deltaRateDegS = do
        let (qL, fL, mL, lzL, pL, vwL) = average velocity incidence (-1) deltaDeg deltaRateDegS (settleFlap velocity incidence (-1) deltaDeg)
            (qR, fR, mR, lzR, pR, vwR) = average velocity incidence 1 deltaDeg deltaRateDegS (settleFlap velocity incidence 1 deltaDeg)
            qd = qL - qR
            mx = x (addVec mL mR)
            fy = y (addVec fL fR)
            lz = lzL + lzR
            power = pL + pR
            vw = vwL + vwR
        putStrLn $ "  " ++ label
          ++ " QL=" ++ f3 qL ++ " QR=" ++ f3 qR ++ " Qd=" ++ f3 qd
          ++ " Mx=" ++ f3 mx ++ " Fy=" ++ f3 fy ++ " Lz=" ++ f3 lz
          ++ " P=" ++ f3 power ++ " VW=" ++ f3 vw
  putStrLn "  (V=8 m/s cruise, amp=20deg, f=2.5Hz, mean over 4 full cycles)"
  row 8.0 (radians 3) "d=0" 0 0
  row 8.0 (radians 3) "d=10deg" 10 0
  row 8.0 (radians 3) "d=20deg" 20 0
  row 8.0 (radians 3) "d=10deg rate=1deg/s" 10 1
  row 8.0 (radians 3) "d=10deg rate=5deg/s" 10 5
  row 8.0 (radians 3) "d=10deg rate=20deg/s" 10 20
  putStrLn "  (V=0 hover flapping, same amplitude/frequency)"
  row 0.0 0 "d=0" 0 0
  row 0.0 0 "d=10deg" 10 0
  row 0.0 0 "d=10deg rate=5deg/s" 10 5

partBodyFollow :: IO ()
partBodyFollow = do
  putStrLn "\n=== PART F: SLOW DIFFERENTIAL IN GLIDE — DOES THE BODY FOLLOW THE WINGS? ==="
  let sim0 = Sim (Quat 1 0 0 0) (Vec3 8 0 0.3) zeroVec
        (defaultFirmwareVehicleState { fvMassFrame = Just (0, iyD, izD) }) 0
      nSteps = round (7.0 / dt) :: Int
      rollAt t = if t < 1 then 0 else min 1 (t / 4.0) * 0.2
      loop n sim | n <= 0 = pure ()
                 | otherwise =
        let t = sT sim
            pilot = PilotInput 0 (rollAt t) 0 0 0.5 True
            (sim', out) = stepSim dt pilot sim
            aL = servoAngleDeg (fvServoLeft (sFv sim'))
            aR = servoAngleDeg (fvServoRight (sFv sim'))
            wingPlaneTilt = (aL - aR) / 2
            up = quatInvRot (sQuat sim') (Vec3 0 0 1)
            bank = atan2 (y up) (z up) * 180 / pi
            hang = fvHangDelta (sFv sim')
        in do
          if n `mod` 60 == 0
            then putStrLn $ "t=" ++ f3 t
              ++ " flapL/R=" ++ f3 aL ++ "/" ++ f3 aR
              ++ " planeTilt=" ++ f3 wingPlaneTilt
              ++ " bank=" ++ f3 bank
              ++ " hang=" ++ f3 hang
              ++ " p=" ++ f3 (x (sOmega sim'))
              ++ " hingeL/R=" ++ f3 (fvLeftHingeTorqueNm (sFv sim')) ++ "/" ++ f3 (fvRightHingeTorqueNm (sFv sim'))
              ++ " flags=" ++ show (outputFlags out)
            else pure ()
          if outputFlags out /= 0 then pure () else loop (n - 1) sim'
  loop nSteps sim0

main :: IO ()
main = do
  hSetEncoding stdout utf8
  putStrLn "AerodynamicRollProbe — slow differential flap: real aero forces & moments"
  putStrLn ("airframeInertia (ix,iy,iz) = " ++ show (ixD, iyD, izD))
  putStrLn ("wing: span=" ++ f3 (wsSpanM (wmWing kestrelModel))
    ++ " shoulder=" ++ f3 (wsShoulderM (wmWing kestrelModel))
    ++ " strips=" ++ show stripCount ++ " rootH=" ++ f3 wingRootHeight)
  putStrLn "Conventions: delta>0 = left wing raised, right lowered."
  putStrLn " Q_code = hingeTorque (conjugate to the stroke angle, positive up);"
  putStrLn " Q_delta = QL - QR (conjugate to delta_L=+d, delta_R=-d);"
  putStrLn " VW column = P + QL*rateL + QR*rateR [virtual work residual, must be ~0]"
  partStatic "A" 8.0
  partStatic "C" 0.0
  partAddedMass
  partFlappingPrescribed
  partFlapping
  partBodyFollow
  putStrLn "\ndone."
