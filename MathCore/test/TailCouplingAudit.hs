module Main where

import Control.Monad (forM_)
import Data.List (sortOn)
import Data.Ord (Down(..))
import Text.Printf (printf)
import Born2Flap.Math.Types (Vec3(..))
import Born2Flap.Math.Firmware
import Born2Flap.Math.FirmwareVehicle
import Born2Flap.Math.Servo
import Born2Flap.Math.Model
import Born2Flap.Math.Airframe
import Born2Flap.Math.Vehicle (VehicleOutput(..), addVec, scaleVec, zeroVec, airDensity, radians)
import qualified Born2Flap.Math.SpatialDynamics as Spatial

components (Vec3 a b c) = [a,b,c]
rotX a (Vec3 x y z) = Vec3 x (cos a*y-sin a*z) (sin a*y+cos a*z)
sumV = foldr addVec zeroVec
force f = sumV [tfLift f,tfPressure f,tfDrag f]
gauss [] = []
gauss rows =
  let p:rs=sortOn (Down . abs . head) rows
      pn=map (/head p) p
      sub=[tail(zipWith (\x y -> x-head r*y) r pn) | r<-rs]
      xs=gauss sub
  in (last pn-sum(zipWith (*) (tail(init pn)) xs)):xs

main :: IO ()
main = do
  let mass=0.05; bank=20; phi=radians bank; dt=1/960
      inertia@(ix,iy,iz)=airframeInertia kestrelModel mass 0
      flow=Vec3 8 0 0
      tail i = kestrelTailLoads i flow zeroVec
      sumTail i = (sumV(map force(tail i)),sumV(map tfMoment(tail i)))
      world=rotX phi
  putStrLn "FIXED LOCAL FLOW: incidence, world force XYZ, world moment XYZ"
  forM_ [-4,-2.3,0,2.3,4] $ \i -> do
    let (f,m)=sumTail i
    printf "%.2f,%s,%s\n" i (show(world f)) (show(world m))
  putStrLn "TAIL FORCE ONLY, LOCKED RELATIVE HINGES: common, density, world angular acceleration XYZ"
  forM_ [-30,-15,0,15,30] $ \common -> forM_ [0,airDensity] $ \rho -> do
    let (matrix,_,_)=Spatial.spatialSystem kestrelModel mass 0 inertia rho
                       (radians(common+bank),radians(common-bank)) (replicate 8 0) zeroVec
        (tm,_)=Spatial.tailFluidInertia 0 rho (radians(-2.3)) (replicate 8 0)
        m6=map (take 6) (take 6(zipWith (zipWith (+)) matrix tm))
        (f,m)=sumTail (-2.3)
        a=gauss(zipWith (\row rhs -> row++[rhs]) m6 (components f++components m))
    printf "%.1f,%.3f,%s\n" common rho (show(world(Vec3(a!!3)(a!!4)(a!!5))))
  putStrLn "FULL SOLVER, SAME INITIAL POSE/FLOW: common, incidence, flags, world acceleration XYZ, world tail force XYZ, tail local velocity"
  forM_ [-30,-15,0,15,30] $ \common -> forM_ [-2.3,0,2.3] $ \i -> do
    let params=defaultFirmwareParams {fwProfile=(fwProfile defaultFirmwareParams)
          {profGlideAngleDeg= -common,profTailElevatorAngleDeg=i,profAileronScale=100}}
        pilot=pilotToRc(PilotInput 0 (-bank/glideAileronMaxDeg) 0 0 0.5 True)
        initial=defaultFirmwareVehicleState {fvServoLeft=ServoState(common+bank) 0,
          fvServoRight=ServoState(common-bank) 0, fvMassFrame=Just(0,iy,iz)}
        (out,next)=stepFirmwareVehicleWithMassInertia mass ix True pilot params
          defaultServoSpec defaultBatterySpec kestrelModel dt flow zeroVec 1000
          (Vec3 0 (sin phi) (cos phi)) initial
        a=scaleVec (1/dt) (fvSolvedBodyRates next)
        tf=fvTailFields next
    printf "%.1f,%.2f,%d,%s,%s,%s\n" common i (outputFlags out) (show(world a))
      (show(world(sumV(map force tf)))) (show(tfVelocity(head tf)))
