-- TailRollAudit: can the tail resist roll during amplitude-differential
-- flapping, and what would a V-cant (dihedral) fan add?
--
-- Part A: static fan response — roll moment Mx (and My/Mz) of the CURRENT
--   flat kestrel fan vs a V-cant fan (halves canted +/-gamma about body X),
--   swept over roll rate p, yaw rate s and sideslip vy at 8 m/s.
-- Part B: full firmware-vehicle solver in flapping flight; step the rudder
--   stick (amplitude differential) and record wing aero roll moment, tail
--   roll moment, body rates p/s, bank and heading.
-- Part C: synthesis — what the V-cant tail would add at the measured yaw rate.
--
-- Run:  cabal exec -- runghc -isrc test/TailRollAudit.hs
module Main (main) where

import Text.Printf (printf)
import Data.List (foldl')

import Born2Flap.Math.Types (Vec3(..))
import Born2Flap.Math.Airframe (tailRootX, tailOutline, tailSamples, airframeInertia)
import Born2Flap.Math.FirmwareVehicle
  ( kestrelTailLoads, TailField(..)
  , FirmwareVehicleState(..), defaultFirmwareVehicleState
  , stepFirmwareVehicleWithMassInertia )
import Born2Flap.Math.Vehicle
  ( addVec, crossVec, scaleVec, magnitude, zeroVec
  , airDensity, radians, surfacePressure, surfaceSkinCd
  , finiteWingLiftSlope, inducedDragCoefficient
  , StripState(..), VehicleOutput(..) )
import Born2Flap.Math.Firmware
  ( PilotInput(..), pilotToRc, defaultFirmwareParams, FirmwareParams(..)
  , FlightProfile(..) )
import Born2Flap.Math.Servo
  ( ServoSpec(..), BatterySpec(..), servoFromNominal, defaultBatterySpec, servoAngleDeg )
import Born2Flap.Math.Model ( kestrelModel )
import qualified Born2Flap.Math.SpatialDynamics as Spatial

-- ---------------------------------------------------------------------------
-- Small vector helpers
-- ---------------------------------------------------------------------------

rotX :: Double -> Vec3 -> Vec3
rotX a (Vec3 x y z) = Vec3 x (cos a * y - sin a * z) (sin a * y + cos a * z)

dotV :: Vec3 -> Vec3 -> Double
dotV (Vec3 a b c) (Vec3 d e f) = a * d + b * e + c * f

-- ---------------------------------------------------------------------------
-- Generic rigid-panel patch load, copied from kestrelTailLoads but with the
-- surface normal and its outline given in the SAME frame as positions/flow.
-- ---------------------------------------------------------------------------

patchLoad :: Vec3 -> [Vec3] -> Vec3 -> Double -> Double -> Vec3 -> Vec3
          -> (Vec3, Vec3)
patchLoad normal outline position patchArea totalArea velocity rates =
  let flow = addVec velocity (crossVec rates position)
      pressureNormalSpeed = dotV flow normal
      sliceFlow = addVec flow (scaleVec (-pressureNormalSpeed) normal)
      sliceSpeed = magnitude sliceFlow
      sliceTangent = if sliceSpeed > 1e-9
        then scaleVec (1 / sliceSpeed) sliceFlow else Vec3 1 0 0
      sliceSpan = crossVec normal sliceTangent
      sliceY = dotV position sliceSpan
      cuts = [ dotV a sliceTangent + f * dotV (addVec b (scaleVec (-1) a)) sliceTangent
             | (a, b) <- zip outline (drop 1 outline ++ take 1 outline)
             , let ya = dotV a sliceSpan
             , let dy = dotV b sliceSpan - ya
             , abs dy > 1e-12
             , let f = (sliceY - ya) / dy
             , f >= 0, f <= 1 ]
      acX = if null cuts then dotV position sliceTangent
            else 0.75 * maximum cuts + 0.25 * minimum cuts
      liftPosition = addVec position (scaleVec (acX - dotV position sliceTangent) sliceTangent)
      liftFlow = addVec velocity (crossVec rates liftPosition)
      normalSpeed = dotV liftFlow normal
      tangentFlow = addVec liftFlow (scaleVec (-normalSpeed) normal)
      tangentSpeed = magnitude tangentFlow
      tangent = if tangentSpeed > 1e-9
        then scaleVec (1 / tangentSpeed) tangentFlow else Vec3 1 0 0
      speed = magnitude liftFlow
      alpha = atan2 (-normalSpeed) tangentSpeed
      spanDirection = crossVec normal tangent
      spans = map (dotV spanDirection) outline
      width = maximum spans - minimum spans
      aspect = width * width / totalArea
      t = max 0 (min 1 ((abs alpha - radians 13) / radians 7))
      separation = t * t * (3 - 2 * t)
      fanSlope = finiteWingLiftSlope aspect * (aspect + 2) / (2 + sqrt (4 + aspect * aspect))
      cl = (1 - separation) * max (-1.9) (min 1.9 (fanSlope * alpha))
      q = 0.5 * airDensity * patchArea * speed * speed
      lift = scaleVec (q * cl / max 1e-9 speed)
               (addVec (scaleVec tangentSpeed normal) (scaleVec (-normalSpeed) tangent))
      pressure = scaleVec (surfacePressure patchArea pressureNormalSpeed) normal
      induced = scaleVec (-0.5 * airDensity * patchArea * speed
                          * inducedDragCoefficient aspect cl) liftFlow
      skin = scaleVec (-0.5 * airDensity * patchArea * magnitude flow * surfaceSkinCd) flow
      drag = addVec induced skin
      moment = addVec (crossVec position (addVec pressure skin))
                      (crossVec liftPosition (addVec lift induced))
  in (addVec lift (addVec pressure drag), moment)

-- ---------------------------------------------------------------------------
-- V-cant fan: left/right halves of the visible polygon, canted +/-gamma about
-- body X (dihedral when gamma > 0). gamma = 0 reproduces the flat fan.
-- ---------------------------------------------------------------------------

rightOutline :: [(Double, Double)]
rightOutline = [ (x, y) | p@(x, y) <- tailOutline, y >= 0 ]

leftOutline :: [(Double, Double)]
leftOutline = [ (x, y) | p@(x, y) <- tailOutline, y <= 0 ]

fanPoint :: Double -> Double -> Double -> Double -> Vec3
fanPoint ci si px py = Vec3 (tailRootX + ci * (px - tailRootX)) py (si * (px - tailRootX))

-- Star-triangulate a polygon from its first vertex (fan root).
samplesOf :: [(Double, Double)] -> [(Double, Double, Double)]
samplesOf outline =
  concat [ points a b c | (b, c) <- zip (drop 1 outline) (drop 2 outline) ]
  where
    a = head outline
    points (ax, ay) (bx, by) (cx, cy) =
      let ar = abs ((bx - ax) * (cy - ay) - (by - ay) * (cx - ax)) / 2
      in [ (ar / 3, wa * ax + wb * bx + wc * cx, wa * ay + wb * by + wc * cy)
         | (wa, wb, wc) <- [ (2/3, 1/6, 1/6), (1/6, 2/3, 1/6), (1/6, 1/6, 2/3) ] ]

vFanLoads :: Double -> Double -> Vec3 -> Vec3 -> [(Vec3, Vec3)]
vFanLoads incidenceDeg gammaDeg velocity rates =
  concatMap half [(-1), 1]
  where
    i = radians incidenceDeg
    ci = cos i
    si = sin i
    half side =
      let raw = if side < 0 then leftOutline else rightOutline
          g = radians (side * gammaDeg)
          samples = samplesOf raw
          totalArea = sum [ a | (a, _, _) <- samples ]
          bodyOutline = [ rotX g (fanPoint ci si px py) | (px, py) <- raw ]
          normal = rotX g (Vec3 (-si) 0 ci)
      in [ patchLoad normal bodyOutline (rotX g (fanPoint ci si px py))
             a totalArea velocity rates
         | (a, px, py) <- samples ]

flatFanLoads :: Double -> Vec3 -> Vec3 -> [(Vec3, Vec3)]
flatFanLoads incidenceDeg velocity rates =
  [ (addVec (tfLift f) (addVec (tfPressure f) (tfDrag f)), tfMoment f)
  | f <- kestrelTailLoads incidenceDeg velocity rates ]

-- ---------------------------------------------------------------------------
-- Part A: derivative table
-- ---------------------------------------------------------------------------

sumMoment :: [(Vec3, Vec3)] -> Vec3
sumMoment = foldl' (\acc (_, m) -> addVec acc m) zeroVec

partA :: IO ()
partA = do
  putStrLn "=== PART A: fan moment response at 8 m/s (Mx roll, My pitch, Mz yaw) [Nm] ==="
  let v = Vec3 8 0 0
  putStrLn "-- roll rate p sweep (rates = p about body X) --"
  putStrLn "   p[rad/s] |  flat Mx      flat Mz    |  V32 Mx      V32 Mz     V20 Mx      V20 Mz"
  forM_ [0, 0.5, 1, 1.5, 2] $ \p ->
    let r = Vec3 p 0 0
        f = sumMoment (flatFanLoads (-2.3) v r)
        c32 = sumMoment (vFanLoads (-2.3) 32 v r)
        c20 = sumMoment (vFanLoads (-2.3) 20 v r)
    in printf "  %8.2f | % .4e % .4e | % .4e % .4e  % .4e % .4e\n"
         p (x f) (z f) (x c32) (z c32) (x c20) (z c20)
  putStrLn "-- yaw rate s sweep (rates = s about body Z, nose right) --"
  putStrLn "   s[rad/s] |  flat Mx     V0 Mx      V32 Mx     est V32 Mx (flat + V32-V0)"
  forM_ [0, 0.5, 1, 1.5, 2] $ \s ->
    let r = Vec3 0 0 s
        f = sumMoment (flatFanLoads (-2.3) v r)
        c0 = sumMoment (vFanLoads (-2.3) 0 v r)
        c32 = sumMoment (vFanLoads (-2.3) 32 v r)
    in printf "  %8.2f | % .4e  % .4e  % .4e   % .4e\n"
         s (x f) (x c0) (x c32) (x f + (x c32 - x c0))
  putStrLn "-- sideslip vy sweep (velocity y component) --"
  putStrLn "  vy[m/s]  |  flat Mx      flat My    |  V32 Mx      V32 My"
  forM_ [0, 1, 2, 3] $ \vy ->
    let u = Vec3 8 vy 0
        f = sumMoment (flatFanLoads (-2.3) u zeroVec)
        c32 = sumMoment (vFanLoads (-2.3) 32 u zeroVec)
    in printf "  %8.2f | % .4e % .4e | % .4e % .4e\n"
         vy (x f) (y f) (x c32) (y c32)
  putStrLn "-- cross-check: V0 (split halves, zero cant) vs flat fan --"
  forM_ [(Vec3 1 0 0), (Vec3 0 0 1)] $ \r ->
    let f = sumMoment (flatFanLoads (-2.3) v r)
        c0 = sumMoment (vFanLoads (-2.3) 0 v r)
    in printf "  rates %s : flat %s  V0 %s\n" (show r) (show f) (show c0)
  putStrLn "-- tail fluid (added-air) inertia: spatial-matrix diagonals --"
  let (tm, _) = Spatial.tailFluidInertia 0 airDensity (radians (-2.3)) (replicate 8 0)
      dr = ixD  -- dry whole-bird roll inertia (incl. wings locked)
  putStrLn ("  dry roll inertia ix = " ++ f3 dr ++ " kg m^2")
  printf "  fan added mass: roll m[3][3] = %.3e, pitch m[4][4] = %.3e, yaw m[5][5] = %.3e kg m^2\n"
    (tm !! 3 !! 3) (tm !! 4 !! 4) (tm !! 5 !! 5)
  printf "  fan added yaw mass is %.1f%% of dry yaw inertia iy=iz=%.3e\n"
    (100 * (tm !! 5 !! 5) / izD) izD

-- ---------------------------------------------------------------------------
-- Part B: full solver, amplitude-differential yaw
-- ---------------------------------------------------------------------------

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
  { sQuat :: !Quat, sVGround :: !Vec3, sOmega :: !Vec3
  , sFv :: !FirmwareVehicleState, sT :: !Double }

massKg :: Double
massKg = 0.050

(ixD, iyD, izD) = airframeInertia kestrelModel massKg 0

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
            , profElevatorScale = 55 }
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
        , profReturnSkew = 0 }
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
  , batteryCRating = 30 }

f3 :: Double -> String
f3 v = printf "% .4f" v

stepSim :: Double -> PilotInput -> Sim -> (Sim, VehicleOutput)
stepSim step pilot sim =
  let bodyUp = quatInvRot (sQuat sim) (Vec3 0 0 1)
      vAirB = quatInvRot (sQuat sim) (sVGround sim)
      rc = pilotToRc pilot
      (out, st') = stepFirmwareVehicleWithMassInertia
        massKg ixD True rc gameParams gameServo gameBattery kestrelModel
        step vAirB (sOmega sim) 1000 bodyUp (sFv sim)
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

aeroWingMx :: FirmwareVehicleState -> Double
aeroWingMx st = sum [ x (stripMoment s) | s <- fvLeftStrips st ++ fvRightStrips st ]

tailMx :: FirmwareVehicleState -> Double
tailMx st = sum [ x (tfMoment f) | f <- fvTailFields st ]

-- Held-pose (wind-tunnel) run: the pose is pinned level, the solver reports
-- the converged body rates and the aero moment split between wings and tail
-- for the commanded amplitude differential. Cycle-mean over the hold window.
runYawHeld :: String -> Double -> Double -> IO ()
runYawHeld label yawStick seconds = do
  putStrLn $ "\n=== PART B (HELD POSE): " ++ label ++ " ==="
  putStrLn "  t[s]  flapL/R     wingMx    tailMx    outMx     p*[rad/s] s*[rad/s]"
  let dt = 1.0 / 240.0
      nSteps = round (seconds / dt) :: Int
      printEvery = 12
      holdFrom = 1.0
      yawAt t = if t < 0.5 then 0 else yawStick
      loop n t st (nA, a, b, c, d, e, f, g)
        | n <= 0 = putStrLn
            ("  --- cycle means: wingMx " ++ f3 (a / nA) ++ " tailMx " ++ f3 (b / nA)
              ++ " outMx " ++ f3 (c / nA) ++ " p* " ++ f3 (d / nA)
              ++ " s* " ++ f3 (e / nA) ++ " flapL/R " ++ f3 (f / nA) ++ "/" ++ f3 (g / nA))
        | otherwise =
            let pilot = PilotInput 0.5 0 0 (yawAt t) 0.5 True
                rc = pilotToRc pilot
                (out, st') = stepFirmwareVehicleWithMassInertia
                  massKg ixD True rc gameParams gameServo gameBattery kestrelModel
                  dt (Vec3 8 0 0.3) zeroVec 1000 (Vec3 0 0 1) st
                aL = servoAngleDeg (fvServoLeft st')
                aR = servoAngleDeg (fvServoRight st')
                p = x (fvSolvedBodyRates st')
                s = z (fvSolvedBodyRates st')
                wMx = aeroWingMx st'
                tMx = tailMx st'
                oMx = x (totalMomentNm out)
                acc' = if t >= holdFrom && t <= seconds - 0.25
                  then (nA + 1, a + wMx, b + tMx, c + oMx, d + p, e + s, f + aL, g + aR)
                  else (nA, a, b, c, d, e, f, g)
            in do
              if n `mod` printEvery == 0
                then putStrLn
                  ("  " ++ f3 t ++ " " ++ f3 aL ++ "/" ++ f3 aR
                    ++ " " ++ f3 wMx ++ " " ++ f3 tMx ++ " " ++ f3 oMx
                    ++ " " ++ f3 p ++ " " ++ f3 s)
                else pure ()
              loop (n - 1) (t + dt) st' acc'
  loop nSteps 0 (defaultFirmwareVehicleState { fvMassFrame = Just (0, iyD, izD) })
    (0, 0, 0, 0, 0, 0, 0, 0)

-- Full six-degree dynamics, small stick so the bird stays upright long enough
-- to read the turn rates and bank coupling.
runYaw :: String -> Double -> Double -> IO ()
runYaw label yawStick seconds = do
  putStrLn $ "\n=== PART C: " ++ label ++ " ==="
  putStrLn "  t[s]  flapL/R   wingMx    tailMx    p[rad/s]  s[rad/s]  bank[deg]  hdg[deg]"
  let sim0 = Sim (Quat 1 0 0 0) (Vec3 8 0 0.3) zeroVec
        (defaultFirmwareVehicleState { fvMassFrame = Just (0, iyD, izD) }) 0
      dt = 1.0 / 240.0
      nSteps = round (seconds / dt) :: Int
      printEvery = 12
      yawAt t = if t < 0.5 then 0 else yawStick
      loop n sim
        | n <= 0 = pure ()
        | otherwise =
            let t = sT sim
                pilot = PilotInput 0.5 0 0 (yawAt t) 0.5 True
                (sim', _) = stepSim dt pilot sim
                st' = sFv sim'
                aL = servoAngleDeg (fvServoLeft st')
                aR = servoAngleDeg (fvServoRight st')
                up = quatInvRot (sQuat sim') (Vec3 0 0 1)
                bank = atan2 (y up) (z up) * 180 / pi
                fwd = quatRot (sQuat sim') (Vec3 1 0 0)
                hdg = atan2 (y fwd) (x fwd) * 180 / pi
            in do
              if n `mod` printEvery == 0
                then putStrLn
                  ("  " ++ f3 t ++ " " ++ f3 aL ++ "/" ++ f3 aR
                    ++ " " ++ f3 (aeroWingMx st') ++ " " ++ f3 (tailMx st')
                    ++ " " ++ f3 (x (sOmega sim')) ++ " " ++ f3 (z (sOmega sim'))
                    ++ " " ++ f3 bank ++ " " ++ f3 hdg)
                else pure ()
              loop (n - 1) sim'
  loop nSteps sim0

-- ---------------------------------------------------------------------------

forM_ :: [a] -> (a -> IO b) -> IO ()
forM_ = flip mapM_

main :: IO ()
main = do
  partA
  runYawHeld "rudder stick +0.20, held level" 0.20 2.5
  runYawHeld "rudder stick +0.40, held level" 0.40 2.5
  runYaw "FLAPPING, rudder stick +0.15 (mild full dynamics)" 0.15 3
  runYaw "FLAPPING, rudder stick -0.15 (opposite sign)" (-0.15) 3