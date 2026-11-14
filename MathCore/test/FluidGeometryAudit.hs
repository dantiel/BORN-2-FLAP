-- Read-only geometry audit: compare the current rigid fluid-inertia diagonal
-- with direct thin-plate kinetic-energy quadrature on a deformed wing.
module Main where
import Control.Monad (forM_)
import Text.Printf (printf)
import Born2Flap.Math.Types (Vec3(..))
import Born2Flap.Math.Vehicle
import Born2Flap.Math.Model
import Born2Flap.Math.Planform
import Born2Flap.Math.Airframe
import qualified Born2Flap.Math.SpatialDynamics as S

dot a b=vx a*vx b+vy a*vy b+vz a*vz b
main=do
  let wp=wmWing kestrelModel;dr=wsSpanM wp/fromIntegral stripCount
      input=VehicleInput (1/960) (Vec3 8 0 (-0.5)) zeroVec 0 0 0 0 1000 (Vec3 0 0 1)
      direct rs=sum [airDensity*pi*c*c*dr/8 * dot n j**2
        | (i,r)<-zip [0..] rs,let f=(fromIntegral i+0.5)/fromIntegral stripCount,
          let c=shapeChord wp f,let (pos,chord,_,n,_,_)=resultFrame r,k<-[-1,1],
          let point=addVec pos (scaleVec (-c/4+k*c/sqrt 32) chord),
          let j=crossVec (Vec3 1 0 0) (addVec point (Vec3 0 0 (-wingRootHeight))) ]
      matrix stroke rho =
        let (m,_,_)=S.spatialSystem kestrelModel 0.05 0.016
              (airframeInertia kestrelModel 0.05 0.016) rho (stroke,stroke) (replicate 8 0) zeroVec
        in m
      reference stroke=matrix stroke airDensity!!7!!7-matrix stroke 0!!7!!7
      eval stroke states=direct(wingLoadsWithStroke False 1 stroke 0 0 input kestrelModel 0 states)
      settle stroke n=iterate (snd . stepCoupledWing False 1 stroke 0 0 0 input kestrelModel 0) initialStrips !! n
  putStrLn "stroke_deg,elastic_twist_deg,rigid_added_inertia,actual_normal_quadrature,ratio"
  forM_ [-25,0,25] $ \a -> forM_ [0,10,45,90] $ \tw -> do
    let stroke=radians a; ref=reference stroke
        value=eval stroke [emptyStrip {stripTwistAero=radians tw} | _<-[1..stripCount]]
    printf "%.1f,%.1f,%.9f,%.9f,%.6f\n" a tw ref value (value/ref)
  putStrLn "Settled loaded geometry (8 m/s, 0.5 m/s descent):"
  forM_ [-25,0,25] $ \a -> do
    let stroke=radians a;s=settle stroke 480; ref=reference stroke;value=eval stroke s
    printf "%.1f tip_twist=%.6f tip_bend=%.6f rigid=%.9f actual=%.9f ratio=%.6f\n"
      a (stripTwistAero(last s)*180/pi) (stripBendM(last s)) ref value (value/ref)
