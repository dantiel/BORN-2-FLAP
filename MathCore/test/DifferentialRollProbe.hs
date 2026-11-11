{-# LANGUAGE OverloadedStrings #-}
-- DifferentialRollProbe: does the fuselage respond to a SLOW differential
-- flap sweep, and is the solver's mechanical coupling right?
--
-- Part A (mechanical): no aero, no gravity. Prescribed smooth ramp
--   dL: 0 -> +20deg, dR: 0 -> -20deg. For a closed rigid system the total
--   x-angular momentum Lx = M33*p + M36*dL' + M37*dR' must stay ZERO, so
--   phi_final = -(M36*dL + M37*dR)/M33. With symmetric wings a pure
--   differential carries no net momentum: phi_final must be ~0.
-- Part B (aero): settled glide, same sweep through the real firmware
--   mixer + servo, gravity on. Records aero roll moment, hinge torques,
--   side force, yaw moment, bank angle.
module Main (main) where

import Data.List (foldl')

import Born2Flap.Math.Types ( Vec3(..) )
import Born2Flap.Math.Vehicle
  ( addVec, scaleVec, zeroVec
  , StripState(..), VehicleOutput(..), airDensity )
import Born2Flap.Math.Firmware
  ( PilotInput(..), pilotToRc, defaultFirmwareParams, FirmwareParams(..)
  , FlightProfile(..) )
import Born2Flap.Math.Servo
  ( ServoSpec(..), BatterySpec(..), defaultServoSpec, defaultBatterySpec
  , servoAngleDeg )
import Born2Flap.Math.Model ( kestrelModel, WingModel(..) )
import Born2Flap.Math.FirmwareVehicle
  ( FirmwareVehicleState(..), defaultFirmwareVehicleState
  , stepFirmwareVehicleWithMassInertia )
import Born2Flap.Math.Airframe ( airframeInertia )
import Born2Flap.Math.Planform
  ( referenceWingMassKg, shapeChord, shapeLeadingEdgeX, wsSpanM, wsShoulderM )
import qualified Born2Flap.Math.SpatialDynamics as Spatial

-- ---------------------------------------------------------------------------
-- Linear algebra + formatting
-- ---------------------------------------------------------------------------

gauss :: [[Double]] -> [Double] -> [Double]
gauss a0 b0 = back (foldl' fwd (a0, b0) [0 .. n - 1])
  where
    n = length b0
    fwd (a, b) i =
      let k = fst (foldl' (\acc@(_, best) r -> let v = abs (a !! r !! i)
                        in if v > best then (r, v) else acc)
                   (i, abs (a !! i !! i)) [i .. n - 1])
          swapRows m = [ if r == i then m !! k
                         else if r == k then m !! i
                         else m !! r | r <- [0 .. n - 1] ]
          a1 = swapRows a
          b1 = [ if r == i then b !! k
                 else if r == k then b !! i
                 else b !! r | r <- [0 .. n - 1] ]
          piv = a1 !! i !! i
          a2 = [ if r <= i then a1 !! r
                 else zipWith (\x xi -> x - xi * (a1 !! r !! i) / piv) (a1 !! r) (a1 !! i)
               | r <- [0 .. n - 1] ]
          b2 = [ if r <= i then b1 !! r
                 else b1 !! r - (a1 !! r !! i) / piv * b1 !! i
               | r <- [0 .. n - 1] ]
      in (a2, b2)
    back (a, b) = snd (foldr (subst b) (a, replicate n 0) [n - 1, n - 2 .. 0])
    subst b i (a, x) =
      let xi = (b !! i - sum [ a !! i !! j * x !! j | j <- [i + 1 .. n - 1] ]) / a !! i !! i
      in (a, [ if j == i then xi else x !! j | j <- [0 .. n - 1] ])

matVec :: [[Double]] -> [Double] -> [Double]
matVec m v = [ sum (zipWith (*) row v) | row <- m ]

f3 :: Double -> String
f3 v =
  let s = if v < 0 then "-" else ""
      a = abs v
      whole = truncate a :: Int
      frac = truncate ((a - fromIntegral whole) * 1000000 + 0.5) :: Int
      fracS = replicate (6 - length (show frac)) '0' ++ show frac
  in s ++ show whole ++ "." ++ take 6 fracS

deg :: Double -> Double
deg r = r * 180 / pi

-- ---------------------------------------------------------------------------
-- Constants (mirror the live game)
-- ---------------------------------------------------------------------------

massKg :: Double
massKg = 0.050

(ixD, iyD, izD) = airframeInertia kestrelModel massKg 0

hingeH :: Double
hingeH = 0.04

wingStations :: [(Double, Double, Double)]
wingStations =
  let wp = wmWing kestrelModel
      n = 128 :: Int
      stations = [ (shapeChord wp f, shapeLeadingEdgeX wp f, wsShoulderM wp + wsSpanM wp * f)
                 | i <- [0 .. n - 1], let f = (fromIntegral i + 0.5) / fromIntegral n ]
      area = sum [ c | (c, _, _) <- stations ]
  in [ (referenceWingMassKg * c / area, r, -lead - c / 2)
     | (c, lead, r) <- stations ]

cov12, cov22 :: Double
cov12 = sum [ m * r | (m, r, _) <- wingStations ]
cov22 = sum [ m * r * r | (m, r, _) <- wingStations ]

-- Correct rigid-hinge flap->roll couplings (flat-plate dry wings about the
-- centreline hinge at (0,0,h), positive flap angle = wing raised):
--   Lx/dTL' = -(cov22 + h*s*cov12) for the LEFT wing  (y<0: rising tip has -Lx)
--   Lx/dTR' = +(cov22 + h*s*cov12) for the RIGHT wing (y>0)
-- A pure differential (left up +d, right down -d) sweeps both wing masses in
-- the SAME rotational sense about x, so the body counter-rolls by
--   phi = 2*(cov22 + h*s*cov12)*d/M33  ~ 0.96*d  (wing plane is the wings).
m36Ref, m37Ref :: Double -> Double
m36Ref s = -(cov22 + hingeH * s * cov12)
m37Ref s = cov22 + hingeH * s * cov12

-- ---------------------------------------------------------------------------
-- Part A: mechanical momentum test
-- ---------------------------------------------------------------------------

-- Integrate the free reduced system  M66*v6' = -(M6f*d2f + bias6)
-- with the prescribed flap ramp. Tracks Lx for the conservation check.
runMechanical :: String -> Double -> Double -> IO ()
runMechanical label rho tRamp = do
  putStrLn $ "\n=== PART A: MECHANICAL FREE SYSTEM (" ++ label
    ++ "), ramp dL:0->+20deg, dR:0->-20deg over " ++ f3 tRamp ++ " s ==="
  let dt = 1.0 / 1000.0
      nSteps = round (tRamp * 2 / dt) :: Int
      dMax = 20 * pi / 180
      su u = u * u * u * (6 * u * u - 15 * u + 10)
      duOf u = if u >= 1 || u <= 0 then 0 else 30 * u * u * (u - 1) * (u - 1) / tRamp
      dduOf u = if u >= 1 || u <= 0 then 0 else 60 * u * (2 * u * u - 3 * u + 1) / tRamp / tRamp
      step (t, v6, phi) =
        let u = min 1 (max 0 (t / tRamp))
            dL = dMax * su u
            dR = -dL
            dLd = dMax * duOf u
            dRd = -dLd
            d2f = [dMax * dduOf u, -dMax * dduOf u]
            v8 = take 6 v6 ++ [dLd, dRd]
            (m, bias, _) = Spatial.spatialSystem kestrelModel massKg 0
              (ixD, iyD, izD) rho (dL, dR) v8 zeroVec
            m66 = [ [ m !! i !! j | j <- [0 .. 5] ] | i <- [0 .. 5] ]
            m6f = [ [ m !! i !! j | j <- [6, 7] ] | i <- [0 .. 5] ]
            rhs = map negate (zipWith (+) (matVec m6f d2f) (take 6 bias))
            dv6 = gauss m66 rhs
            v6' = zipWith (+) v6 [ x * dt | x <- dv6 ]
            phi' = phi + v6' !! 3 * dt
            lx = sum [ m !! 3 !! j * v8 !! j | j <- [0 .. 7] ]
        in (t + dt, v6', phi', lx)
      loop :: Int -> (Double, [Double], Double) -> IO (Double, [Double], Double)
      loop n acc@(t, v6, phi) | n <= 0 = pure acc
                              | otherwise = do
        let (t', v6', phi', lx) = step acc
        if n `mod` 500 == 0
          then putStrLn $ "t=" ++ f3 t' ++ " phi=" ++ f3 (deg phi')
            ++ " p=" ++ f3 (v6' !! 3) ++ " Lx=" ++ f3 lx
          else pure ()
        loop (n - 1) (t', v6', phi')
  (_, _, phiEnd) <- loop nSteps (0.0, replicate 6 0, 0.0)
  putStrLn $ "  phi_final = " ++ f3 (deg phiEnd) ++ " deg"
  let s = sin (20 * pi / 180)
      (mEnd, _, _) = Spatial.spatialSystem kestrelModel massKg 0
        (ixD, iyD, izD) rho (20 * pi / 180, -20 * pi / 180) (replicate 8 0) zeroVec
      m33 = mEnd !! 3 !! 3
      m36 = mEnd !! 3 !! 6
      m37 = mEnd !! 3 !! 7
      sL = sin dMax
      sR = sin (-dMax)
      predCode = -(m36 * dMax + m37 * (-dMax)) / m33
      predRef = -(m36Ref sL * dMax + m37Ref sR * (-dMax)) / m33
  putStrLn $ "  code M at end: M33=" ++ f3 m33 ++ " M36=" ++ f3 m36
    ++ " M37=" ++ f3 m37 ++ " -> quasi-static phi_final=" ++ f3 (deg predCode) ++ " deg"
  putStrLn $ "  ref  M at end: M36ref=" ++ f3 (m36Ref sL) ++ " M37ref=" ++ f3 (m37Ref sR)
    ++ " -> quasi-static phi_final=" ++ f3 (deg predRef) ++ " deg"
    ++ " (momentum conservation: body counter-rolls ~1:1 with the differential)"

-- Patched-matrix variant: correct M36/M37, zero bias (linear system). The
-- exact rigid prediction phi(t) = -(M36*dL+M37*dR)/M33 must hold.
runMechanicalPatched :: IO ()
runMechanicalPatched = do
  putStrLn "\n=== PART A': PATCHED MASS MATRIX (reference hinge couplings, bias=0) ==="
  let tRamp = 4.0
      dt = 1.0 / 1000.0
      nSteps = round (tRamp * 2 / dt) :: Int
      dMax = 20 * pi / 180
      (m0, _, _) = Spatial.spatialSystem kestrelModel massKg 0
        (ixD, iyD, izD) 0 (0, 0) (replicate 8 0) zeroVec
      patch i j v m = [ [ if (r == i && c == j) || (r == j && c == i) then v else m !! r !! c
                        | c <- [0 .. 7] ] | r <- [0 .. 7] ]
      mPatched = foldl' (\m (i, j, v) -> patch i j v m) m0
        [ (3, 6, m36Ref 0), (3, 7, m37Ref 0) ]
      m66 = [ [ mPatched !! i !! j | j <- [0 .. 5] ] | i <- [0 .. 5] ]
      m6f = [ [ mPatched !! i !! j | j <- [6, 7] ] | i <- [0 .. 5] ]
      step (t, v6, phi) =
        let u = min 1 (max 0 (t / tRamp))
            ddu = if u >= 1 || u <= 0 then 0
                  else 60 * u * (2 * u * u - 3 * u + 1) / tRamp / tRamp
            d2f = [dMax * ddu, -dMax * ddu]
            dv6 = gauss m66 (map negate (matVec m6f d2f))
            v6' = zipWith (+) v6 [ x * dt | x <- dv6 ]
            phi' = phi + v6' !! 3 * dt
        in (t + dt, v6', phi')
      loop n acc | n <= 0 = acc
                 | otherwise = loop (n - 1) (step acc)
      (_, _, phiEnd) = loop nSteps (0.0, replicate 6 0, 0.0)
      predRef = -(m36Ref 0 * dMax + m37Ref 0 * (-dMax)) / (m0 !! 3 !! 3)
  putStrLn $ "  phi_final = " ++ f3 (deg phiEnd) ++ " deg"
  putStrLn $ "  rigid prediction = " ++ f3 (deg predRef)
    ++ " deg (both wing masses sweep the same rotational sense: net Lx != 0, body counter-rolls)"

-- ---------------------------------------------------------------------------
-- Part B: full firmware glide
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
stepQuat q (Vec3 px py pz) dt =
  let half = 0.5 * dt
  in quatNorm (quatMul q (Quat 1 (half * px) (half * py) (half * pz)))

data Sim = Sim
  { sQuat    :: !Quat
  , sVGround :: !Vec3
  , sOmega   :: !Vec3
  , sFv      :: !FirmwareVehicleState
  , sT       :: !Double
  }

dt :: Double
dt = 1.0 / 240.0

aeroRollMx :: FirmwareVehicleState -> Double
aeroRollMx st = sum [ x (stripMoment s) | s <- fvLeftStrips st ++ fvRightStrips st ]

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

runGlideSweep :: String -> Double -> Double -> Double -> IO ()
runGlideSweep label rampSecs holdStick seconds = do
  putStrLn $ "\n=== PART B: GLIDE " ++ label ++ " ==="
  let sim0 = Sim (Quat 1 0 0 0) (Vec3 8 0 0.3) zeroVec
        (defaultFirmwareVehicleState { fvMassFrame = Just (0, iyD, izD) }) 0
      nSteps = round (seconds / dt) :: Int
      printEvery = 12  -- 20 Hz
      rollAt t = if t < 1 then 0 else min 1 (t / rampSecs) * holdStick
      loop :: Int -> Sim -> IO ()
      loop n sim | n <= 0 = pure ()
                 | otherwise =
        let t = sT sim
            pilot = PilotInput 0 (rollAt t) 0 0 0.5 True
            (sim', out) = stepSim dt pilot sim
            st' = sFv sim'
            aL = servoAngleDeg (fvServoLeft st')
            aR = servoAngleDeg (fvServoRight st')
            hL = fvLeftHingeTorqueNm st'
            hR = fvRightHingeTorqueNm st'
            hang = fvHangDelta st'
            up = quatInvRot (sQuat sim') (Vec3 0 0 1)
            bank = atan2 (y up) (z up) * 180 / pi
            fW = quatRot (sQuat sim') (totalForceN out)
            mB = totalMomentNm out
            aero = aeroRollMx st'
        in do
          if n `mod` printEvery == 0
            then putStrLn $ "t=" ++ f3 t
              ++ " flap=" ++ f3 aL ++ "/" ++ f3 aR
              ++ " bank=" ++ f3 bank
              ++ " p=" ++ f3 (x (sOmega sim'))
              ++ " Mx_out=" ++ f3 (x mB)
              ++ " Mx_aero=" ++ f3 aero
              ++ " hang=" ++ f3 hang
              ++ " hingeNm=" ++ f3 hL ++ "/" ++ f3 hR
              ++ " Fy=" ++ f3 (y fW)
              ++ " Mz=" ++ f3 (z mB)
              ++ " FzW=" ++ f3 (z fW)
              ++ " flags=" ++ show (outputFlags out)
            else pure ()
          loop (n - 1) sim'
  loop nSteps sim0

main :: IO ()
main = do
  putStrLn ("airframeInertia (ix,iy,iz) = " ++ show (ixD, iyD, izD))
  putStrLn ("wing quadrature: cov12=" ++ f3 cov12 ++ " cov22=" ++ f3 cov22)
  putStrLn ("ref couplings at flat: M36ref=" ++ f3 (m36Ref 0)
    ++ " M37ref=" ++ f3 (m37Ref 0) ++ " (opposite signs, same magnitude)")
  putStrLn "=== CODE MASS MATRIX COUPLINGS at flat (0,0) ==="
  let dump rho label =
        let (m, _, _) = Spatial.spatialSystem kestrelModel massKg 0
              (ixD, iyD, izD) rho (0, 0) (replicate 8 0) zeroVec
        in putStrLn $ "  code " ++ label ++ ": M33=" ++ f3 (m !! 3 !! 3)
             ++ " M36=" ++ f3 (m !! 3 !! 6) ++ " M37=" ++ f3 (m !! 3 !! 7)
             ++ " M66=" ++ f3 (m !! 6 !! 6) ++ " M67=" ++ f3 (m !! 6 !! 7)
             ++ " M26=" ++ f3 (m !! 2 !! 6) ++ " M27=" ++ f3 (m !! 2 !! 7)
  dump 0 "dry"
  dump airDensity "wet"
  runMechanical "DRY (density 0)" 0 4.0
  runMechanical "WET (density 1.05)" airDensity 4.0
  runMechanical "DRY FAST RAMP" 0 0.2
  runMechanicalPatched
  runGlideSweep "SLOW differential (stick ramp 4 s to 0.286)" 4.0 0.286 7
  runGlideSweep "FAST differential (stick step to 0.286)" 0.05 0.286 7