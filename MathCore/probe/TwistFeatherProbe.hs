module Main where

import Born2Flap.Math.Vehicle
import Born2Flap.Math.Model (kestrelModel)
import Born2Flap.Math.Types (Vec3(..))
import Born2Flap.Math.Firmware
import Born2Flap.Math.FirmwareVehicle
import Born2Flap.Math.Servo (defaultServoSpec, defaultBatterySpec)

mkInput :: Double -> Vec3 -> VehicleInput
mkInput vy rates = VehicleInput
  { stepSeconds = 0.002
  , bodyVelocityMS = Vec3 8 vy 0
  , bodyRatesRadS = rates
  , throttleCommand = 0.75
  , rollCommand = 0
  , pitchCommand = 0
  , yawCommand = 0
  , groundClearanceM = 100
  , bodyUpB = Vec3 0 0 1
  }

-- Converge the quasi-static elastic twist (250 coupled steps).
conv :: Double -> Double -> VehicleInput -> Int -> ([StripResult], [StripState])
conv side rate inp n =
  iterate (\(_, sts) -> stepCoupledWing True side 0.35 rate rate rate inp kestrelModel 0 sts)
          (stepCoupledWing True side 0.35 rate rate rate inp kestrelModel 0 initialStrips) !! n

wingAero :: Double -> Double -> VehicleInput -> (Double, Double, Double)
wingAero side rate inp =
  let (rs, _) = conv side rate inp 250
      tip = last rs
      twistDeg = stripTwistAero (resultState tip) * 180 / pi
      fz = sum (map (vz . resultForce) rs)
      fx = sum (map (vx . resultForce) rs)
  in (fz, fx, twistDeg)

-- Sideslip sweep at the downstroke: per-wing vertical force. Effective
-- dihedral means the UPWIND wing carries more lift.
sideslipSweep :: IO ()
sideslipSweep = mapM_ (\vy -> do
    let (fzR, _, _) = wingAero 1 (-20) (mkInput vy (Vec3 0 0 0))
        (fzL, _, _) = wingAero (-1) (-20) (mkInput vy (Vec3 0 0 0))
    putStrLn (unwords [ "SIDESLIP vy=" ++ show vy
                      , "FzRightN=" ++ show fzR, "FzLeftN=" ++ show fzL ]))
  [-3, 0, 3]

-- One wing through a full phase pair.
phaseAero :: IO ()
phaseAero = do
  let (fzD, fxD, twD) = wingAero 1 (-20) (mkInput 0 (Vec3 0 0 0))
      (fzU, fxU, twU) = wingAero 1 20 (mkInput 0 (Vec3 0 0 0))
  putStrLn (unwords [ "DOWNSTROKE FzN=" ++ show fzD, "FxN=" ++ show fxD, "twistDeg=" ++ show twD ])
  putStrLn (unwords [ "UPSTROKE   FzN=" ++ show fzU, "FxN=" ++ show fxU, "twistDeg=" ++ show twU ])

-- Gyro: inject a solved roll rate into the state with NEUTRAL stick. The
-- damper must oppose it: p>0 (right wing rising) -> negative aileron -> Mx>0.
gyroProbe :: IO ()
gyroProbe = do
  let rc = defaultRcChannels { rcThrottle = 1500 }
      st = defaultFirmwareVehicleState { fvSolvedBodyRates = Vec3 2 0 0 }
      (out, _) = stepFirmwareVehicleWithMassInertia 0.05 1.0e-4 True rc
                   defaultFirmwareParams defaultServoSpec defaultBatterySpec kestrelModel
                   0.002 (Vec3 8 0 0) (Vec3 0 0 0) 100 (Vec3 0 0 1) st
  putStrLn (unwords [ "GYRO p=+2 MxNm=" ++ show (x (totalMomentNm out))
                    , "FxN=" ++ show (x (totalForceN out)) ])

-- Firmware vehicle: full-stick aileron both ways, read the total roll moment.
fwRoll :: Double -> (Double, Double)
fwRoll aileronRaw =
  let rc = defaultRcChannels { rcThrottle = 1500, rcAileron = aileronRaw }
      (out, _) = stepFirmwareVehicleWithMassInertia 0.05 1.0e-4 True rc
                   defaultFirmwareParams defaultServoSpec defaultBatterySpec kestrelModel
                   0.002 (Vec3 8 0 0) (Vec3 0 0 0) 100 (Vec3 0 0 1)
                   defaultFirmwareVehicleState
  in (x (totalMomentNm out), x (totalForceN out))

firmwareRoll :: IO ()
firmwareRoll = do
  let (mxL, _) = fwRoll (normToRaw (-1))
      (mxR, _) = fwRoll (normToRaw 1)
  putStrLn (unwords [ "FW ROLL aileron=-1 MxNm=" ++ show mxL
                    , "aileron=+1 MxNm=" ++ show mxR ])

-- Multi-step: let the servo track the full stroke, average the roll moment
-- over the last 30 of 60 steps -- the cyclic amplitude channel shows here.
fwRollMean :: Double -> Double
fwRollMean an =
  let rc = defaultRcChannels { rcThrottle = 1500, rcAileron = normToRaw an }
      step0 = stepFirmwareVehicleWithMassInertia 0.05 1.0e-4 True rc
                defaultFirmwareParams defaultServoSpec defaultBatterySpec kestrelModel
                0.002 (Vec3 8 0 0) (Vec3 0 0 0) 100 (Vec3 0 0 1)
                defaultFirmwareVehicleState
      step (out, st) = stepFirmwareVehicleWithMassInertia 0.05 1.0e-4 True rc
                         defaultFirmwareParams defaultServoSpec defaultBatterySpec kestrelModel
                         0.002 (Vec3 8 0 0) (Vec3 0 0 0) 100 (Vec3 0 0 1) st
      outs = iterate step step0
      moments = map (x . totalMomentNm . fst) (take 60 outs)
      last30 = drop 30 moments
  in sum last30 / fromIntegral (length last30)

firmwareRollMean :: IO ()
firmwareRollMean = do
  putStrLn (unwords [ "FW ROLL MEAN aileron=-1 MxNm=" ++ show (fwRollMean (-1))
                    , "aileron=+1 MxNm=" ++ show (fwRollMean 1) ])

-- Mixer: commanded wing angles at the same phase, aileron -1 / 0 / +1.
mixerProbe :: IO ()
mixerProbe = mapM_ (\an -> do
    let rc = defaultRcChannels { rcThrottle = 1500, rcAileron = normToRaw an }
        (m, _) = computeServoMixer True rc defaultFirmwareParams defaultFirmwareState 0.002
    putStrLn (unwords [ "MIXER aileron=" ++ show an
                      , "leftDeg=" ++ show (mixLeftWingDeg m)
                      , "rightDeg=" ++ show (mixRightWingDeg m) ]))
  [-1, 0, 1]

main :: IO ()
main = do
  putStrLn "== PHASE AERO (one wing, vy=0) =="
  phaseAero
  putStrLn "== SIDESLIP SWEEP (downstroke) =="
  sideslipSweep
  putStrLn "== FIRMWARE ROLL =="
  firmwareRoll
  putStrLn "== FIRMWARE ROLL MEAN (60 steps) =="
  firmwareRollMean
  putStrLn "== GYRO =="
  gyroProbe
  putStrLn "== MIXER =="
  mixerProbe