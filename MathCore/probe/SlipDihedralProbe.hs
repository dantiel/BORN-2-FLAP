-- Effective-dihedral / sideslip righting probe.
-- Glides the kestrel wing at 8 m/s with body sideslip vy and measures the
-- lift asymmetry between the two wings (upwind wing must carry MORE lift)
-- plus the resulting roll moment. Also tests a small geometric anhedral
-- (negative stroke centre) to prove the righting term outweighs it.
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
      settle side stroke n =
        let input = VehicleInput (1/960) (Vec3 8 0 (-0.5)) zeroVec 0 0 0 0 1000 (Vec3 0 0 1)
        in iterate (snd . stepCoupledWing False side stroke 0 0 0 input kestrelModel 0) initialStrips !! n
      wingFz side stroke vy =
        let input = VehicleInput (1/960) (Vec3 8 vy (-0.5)) zeroVec 0 0 0 0 1000 (Vec3 0 0 1)
            s = iterate (snd . stepCoupledWing False side stroke 0 0 0 input kestrelModel 0) initialStrips !! 480
            rs = wingLoadsWithStroke False side stroke 0 0 input kestrelModel 0 s
        in sum (map (vz . resultForce) rs)
      wingMx side stroke vy =
        let input = VehicleInput (1/960) (Vec3 8 vy (-0.5)) zeroVec 0 0 0 0 1000 (Vec3 0 0 1)
            s = iterate (snd . stepCoupledWing False side stroke 0 0 0 input kestrelModel 0) initialStrips !! 480
            rs = wingLoadsWithStroke False side stroke 0 0 input kestrelModel 0 s
        in sum (map (vx . stripMoment . resultState) rs)
  putStrLn "stroke_centre_rad,vy,Fz_right,Fz_left,asym,rollMx"
  forM_ [0, (-0.05)] $ \stroke -> do
    forM_ [0, 1, 2, 3] $ \vy -> do
      let fr = wingFz 1 stroke vy
          fl = wingFz (-1) stroke vy
          mx = wingMx 1 stroke vy + wingMx (-1) stroke vy
      printf "%.3f,%.1f,%.4f,%.4f,%.4f,%.4f\n" stroke vy fr fl (fr-fl) mx
