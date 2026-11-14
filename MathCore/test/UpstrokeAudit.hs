module Main where

import Control.Monad (forM_, unless)
import Text.Printf (printf)
import Born2Flap.Math.Types (Vec3(..))
import Born2Flap.Math.Vehicle
import Born2Flap.Math.Model
import Born2Flap.Math.Planform
import Born2Flap.Math.Section
import Born2Flap.Math.Airframe
import qualified Born2Flap.Math.SpatialDynamics as Spatial

sumV = foldr addVec zeroVec
dot (Vec3 a b c) (Vec3 x y z) = a*x+b*y+c*z
hinge force position = vx (crossVec (addVec position (Vec3 0 0 (-wingRootHeight))) force)

-- Freeze the same loaded geometry and stall/LEV history while varying
-- shoulder speed. This measures a local derivative, not a periodic flight.
main :: IO ()
main = do
  putStrLn "u,stroke_deg,rate_rad_s,Fz_circulation,Fz_pressure,Fz_drag,Q_circulation,Q_pressure,Q_drag,Q_total,aero_power,tip_twist_deg"
  forM_ [2,8] $ \u -> forM_ [-25,0,25] $ \angle -> do
    let stroke=radians angle
        input=VehicleInput (1/960) (Vec3 u 0 (-0.5)) zeroVec 0 0 0 0 1000 (Vec3 0 0 1)
        settle 0 s=s
        settle n s=settle (n-1) (snd(stepCoupledWing False 1 stroke 0 0 0 input kestrelModel 0 s))
        loaded=settle (480::Int) initialStrips
        frozen=map (\s -> s {stripBendRateMS=0,stripTwistRateRadS=0,stripBendSlopeRate=0}) loaded
        loads rate states=wingLoadsWithStroke False 1 stroke rate rate input kestrelModel 0 states
    forM_ [-8,-2,-0.2,0,0.2,2,8] $ \rate -> do
      let rs=loads rate frozen
          force part=sumV[part(resultState r)|r<-rs]
          torque part=sum[hinge (part s) (stripPosition s)|r<-rs,let s=resultState r]
          totalQ=sum[vx(resultMoment r)+wingRootHeight*vy(resultForce r)|r<-rs]
      printf "%.1f,%.1f,%.6f,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f,%.6f\n"
        u angle rate (vz(force stripLift)) (vz(force stripPressure)) (vz(force stripDrag))
        (torque stripLift) (torque stripPressure) (torque stripDrag) totalQ (-rate*totalQ)
        (stripTwistAero(last frozen)*180/pi)
    -- The exact twist generalized load follows virtual work with the actual
    -- geometry, independently of an elastic load multiplier or stiffness.
    let eps=1e-6
        plus=loads 2 [s {stripTwistAero=stripTwistAero s+eps}|s<-frozen]
        minus=loads 2 [s {stripTwistAero=stripTwistAero s-eps}|s<-frozen]
        base=loads 2 frozen
        errors=zipWith3 (\r p m ->
          let s=resultState r; (_,_,spanAxis,_,_,_)=resultFrame r
              angular=scaleVec (-1) spanAxis
              sectionCouple=addVec(resultMoment r)(scaleVec (-1)(crossVec(stripPosition s)(resultForce r)))
              jac=scaleVec (1/(2*eps))(addVec(stripPosition(resultState p))(scaleVec (-1)(stripPosition(resultState m))))
              reference=dot(resultForce r) jac+dot sectionCouple angular
          in abs(resultSectionMoment r-reference)/max 1e-9 (abs reference)) base plus minus
    unless (maximum errors<1e-5) $ fail ("twist virtual-work mismatch: "++show (maximum errors))
  putStrLn "INERTIA: common wing acceleration requires dry, added-fluid and drive inertia, with identical up/down signs"
  let inertia=airframeInertia kestrelModel 0.05 0.016
      system rho=let (m,_,_)=Spatial.spatialSystem kestrelModel 0.05 0.016 inertia rho (0,0) (replicate 8 0) zeroVec in m
      dry=system 0;wet=system airDensity
  printf "right_hinge_diagonal_dry_plus_drive=%.9f added_air=%.9f Nm_per_rad_s2\n"
    (dry!!7!!7) (wet!!7!!7-dry!!7!!7)
  putStrLn "PASS: twist virtual work at loaded dihedral/horizontal/anhedral geometries"
