{-# LANGUAGE OverloadedStrings #-}
module Main where

import Control.Monad (forM_, unless)
import Text.Printf (printf)
import Born2Flap.Math.Types (Vec3(..))
import Born2Flap.Math.Vehicle
import Born2Flap.Math.Model
import Born2Flap.Math.Planform
import Born2Flap.Math.Section
import Born2Flap.Math.Firmware
import Born2Flap.Math.FirmwareVehicle
import Born2Flap.Math.Servo
import Born2Flap.Math.Airframe
import qualified Born2Flap.Math.SpatialDynamics as Spatial

-- Independent small-disturbance reference:
-- delta alpha = -r*qdot/U, dL = rho*U^2*c*a*delta alpha*dr/2.
-- Hence dQ/dqdot = -rho*U*a*c*b^3/6 for one rectangular semispan b.
-- a = 2*pi/(1+2/AR) is lifting-line's elliptic-loading estimate.
-- This reference does not call the production polar or force projection.
main :: IO ()
main = do
  putStrLn "RECTANGULAR WING: U, measured dQ/dRate, lifting-line reference, ratio"
  let b=0.6; c=0.2; rho=airDensity; ar=2*b/c
      ref u = -rho*u*(2*pi/(1+2/ar))*c*b^3/6
      wp0=wmWing kestrelModel
      station f=(head (wsStations wp0)) {ssFraction=f,ssLeadX=0,ssChordM=c,
                    ssTwistDeg=0,ssDihedDeg=0,
                    ssSection=SectionProfile 0 0 0 0.02}
      model=kestrelModel {wmWing=wp0 {wsSpanM=b,wsShoulderM=0,
                     wsMountAngleDeg=0,wsArmAngleDeg=0,wsStations=[station 0,station 1]}}
      loads u rate = wingTrialMotion
        (prepareWingSystem False 1 0 0
          (VehicleInput (1/960) (Vec3 u 0 0) zeroVec 0 0 0 0 1000 (Vec3 0 0 1))
          model 0 [s {stripCamber=0} | s<-initialStrips])
        zeroVec zeroVec rate 0 (0,0)
      q u rate=hingeTorque 1 (loads u rate)
      eps=1e-5
  forM_ [2,4,8,12] $ \u -> do
    let derivative=(q u eps-q u (-eps))/(2*eps)
        ratio=derivative/ref u
    printf "%.1f, %.9f, %.9f, %.6f\n" u derivative (ref u) ratio
    -- The production model explicitly uses 85 percent circulation efficiency.
    -- Bound quadrature and profile-drag effects without fitting to its slope.
    unless (ratio>0.83 && ratio<0.89) $ fail "small-rate circulation disagrees with lifting-line reference"
  putStrLn "HOST HANDOFF: test converged rates against the actual returned host impulse"
  let mass=0.05
      (ix,iy,iz)=airframeInertia kestrelModel mass 0
      dt=1/960
      params=defaultFirmwareParams
      initial=defaultFirmwareVehicleState {fvMassFrame=Just (0,iy,iz)}
      rates=Vec3 0.03 (-0.02) 0.01
      run n state=do
        let (out,next)=stepFirmwareVehicleWithMassInertia mass ix True
              (pilotToRc (PilotInput 0 0.15 0 0 0.5 True)) params
              defaultServoSpec defaultBatterySpec kestrelModel dt
              (Vec3 8 0 (-0.2)) rates 1000 (Vec3 0 0 1) state
            Vec3 mx my mz=totalMomentNm out
            Vec3 px py pz=rates
            host=Vec3 (px+dt*mx/ix) (py+dt*my/iy) (pz+dt*mz/iz)
            err=magnitude (addVec host (scaleVec (-1) (fvSolvedBodyRates next)))
        unless (outputFlags out==0 && err<1e-11) $ fail ("host impulse mismatch: "++show err)
        if n==0 then printf "PASS residual %.3g rad/s, holding/driving torques %s Nm\n" err (show (fvDriveTorques next))
          else run (n-1) next
  run (100::Int) initial
  -- A stationary servo under a finite load must exert a finite holding torque.
  let holding=servoDriveTorque defaultServoSpec defaultBatterySpec 7.4 (1/960)
                0 0.02 0.001 defaultServoState 0
  printf "Stationary servo holding torque %.9f Nm for +0.02 Nm load\n" holding
  unless (abs (holding+0.02)<1e-10) $ fail "servo failed static holding load"
  putStrLn "ROLL INERTIA LEDGER: locked and world-stationary-wing motions"
  let (dry,_,_)=Spatial.spatialSystem kestrelModel mass 0 (ix,iy,iz) 0 (0,0) (replicate 8 0) zeroVec
      (wet,_,_)=Spatial.spatialSystem kestrelModel mass 0 (ix,iy,iz) airDensity (0,0) (replicate 8 0) zeroVec
      stationary=[0,0,0,1,0,0,1,-1]
      quadratic m v=sum(zipWith (*) v (Spatial.multiply m v))
      -- Rotor reflected energy belongs to joint motion, not body momentum.
      bodyDry=sum(zipWith (*) (dry!!3) stationary)
      bodyWet=sum(zipWith (*) (wet!!3) stationary)
  printf "dry locked %.9g wet locked %.9g body-row dry %.9g body-row wet %.9g\n"
    (dry!!3!!3) (wet!!3!!3) bodyDry bodyWet
  printf "stationary-wing kinetic coefficient %.9g (including %.9g drive inertia)\n"
    (quadratic wet stationary) (2*reflectedDriveInertia)
  let (kick,kicked)=stepFirmwareVehicleWithMassInertia mass ix True
        (pilotToRc(PilotInput 0 0.25 0 0 0.5 True)) defaultFirmwareParams
        defaultServoSpec defaultBatterySpec kestrelModel dt zeroVec zeroVec 1000 zeroVec initial
      p=vx(fvSolvedBodyRates kicked)
      l=servoRateDegPerSec(fvServoLeft kicked)*pi/180
      r=servoRateDegPerSec(fvServoRight kicked)*pi/180
  printf "gravity-free still-air kick: p=%.9g l=%.9g r=%.9g body/diff=%.9g flags=%d\n" p l r (p/((l-r)/2)) (outputFlags kick)
