module Main where
import Control.Monad (forM_, unless)
import Text.Printf (printf)
import Born2Flap.Math.Servo

main :: IO ()
main = do
  let servo=servoFromNominal "captured servo" 857.142883 0.74 20
      dt=1/240; inertia=0.00098
      drive target load=servoDriveTorque servo defaultBatterySpec 7.4 dt target load inertia defaultServoState 0
  putStrLn "load_Nm,motor_up_Nm,net_up_Nm,motor_down_Nm,net_down_Nm,holding_Nm"
  forM_ [0,0.1,0.3,0.5,0.7,0.74] $ \load -> do
    printf "%.6f,%.6f,%.6f,%.6f,%.6f,%.6f\n" load
      (drive 60 load) (drive 60 load+load) (drive (-60) load) (drive (-60) load+load) (drive 0 load)
    -- Zero speed, far from either target: an assisting load cannot turn a
    -- full drive request into braking. The same load must still be held at
    -- rest, and opposed at the same stall-torque limit when driving down.
    unless (abs(drive 60 load-0.74)<1e-9 && abs(drive (-60) load+0.74)<1e-9 &&
            abs(drive 0 load+load)<1e-9) $ fail "assisting aerodynamic load was treated as motor resistance"
  -- Independent DC motor reference while far from the target:
  -- I*w' = Tstall*(1-w/wfree)+load, hence
  -- w(t) = wfree*(1+load/Tstall)*(1-exp(-Tstall*t/(I*wfree))).
  -- Integrate the actual torque controller through a midpoint joint equation,
  -- including its reaction to a helping load and both mirrored directions.
  let h=1/9600; count=192::Int; totalI=inertia+reflectedDriveInertia
      spin target load = go count defaultServoState
        where
          go 0 s=servoRateDegPerSec s*pi/180
          go n s=
            let w0=servoRateDegPerSec s*pi/180
                trial w=w0+h*(servoDriveTorque servo defaultBatterySpec 7.4 h target load inertia s ((w0+w)/2)+load)/totalI
                w1=iterate trial w0 !! 12
                s1=ServoState (servoAngleDeg s+h*(w0+w1)/2*180/pi) (w1*180/pi)
            in go (n-1) s1
      free=servoSpeedAt servo 7.4*pi/180
      expected load=free*(1+load/0.74)*(1-exp(-0.74*(h*fromIntegral count)/(totalI*free)))
  forM_ [0,0.1,0.3,0.5,0.7] $ \load -> forM_ [-1,1] $ \direction -> do
    let actual=spin (direction*60) (direction*load)
        reference=direction*expected load
    unless (abs(actual-reference)<1e-5) $ fail ("DC motor spin-up mismatch: "++show(actual,reference))
  putStrLn "PASS: assisting/opposing/holding torque and independent signed DC motor spin-up"

