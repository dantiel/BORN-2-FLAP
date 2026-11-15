-- High-flap-speed twist / stream-redirection probe.
-- Measures settled tip twist, total vertical force and thrust for a flapping
-- wing at +/-20 rad/s stroke rate, and decomposes lift vs broadside pressure.
module Main where
import Control.Monad (forM_)
import Text.Printf (printf)
import Born2Flap.Math.Types (Vec3(..))
import Born2Flap.Math.Vehicle
import Born2Flap.Math.Model
import Born2Flap.Math.Planform

main :: IO ()
main = do
  let wp = wmWing kestrelModel
      input = VehicleInput (1/960) (Vec3 8 0 (-0.5)) zeroVec 0 0 0 0 1000 (Vec3 0 0 1)
      settle flapping side stroke rate n =
        iterate (snd . stepCoupledWing flapping side stroke rate 0 0 input kestrelModel 0) initialStrips !! n
      totals rs = foldr step0 (0,0,0,0) (zip [0..] rs)
      step0 (i,r) (fz,fx,lz,pz) =
        (fz+vz(resultForce r), fx+vx(resultForce r),
         lz+vz(stripLift (resultState r)), pz+vz(stripPressure (resultState r)))
  putStrLn "rate,stroke,tip_twist_deg,tip_bend_m,Fz_total,Fx_total,lift_Fz,press_Fz"
  forM_ [(-20.0),20.0] $ \rate -> do
    let s = settle True 1 0 rate 480
        r = wingLoadsWithStroke True 1 0 rate 0 input kestrelModel 0 s
        (fz,fx,lz,pz) = totals r
        tip = last s
    printf "%.1f,mid,%.3f,%.6f,%.4f,%.4f,%.4f,%.4f\n"
      rate (stripTwistAero tip*180/pi) (stripBendM tip) fz fx lz pz
