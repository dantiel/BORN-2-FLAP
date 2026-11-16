module Main (main) where

import Data.List (foldl')

import Born2Flap.Math.Types ( Vec3(..) )
import Born2Flap.Math.Vehicle
import Born2Flap.Math.Model ( kestrelModel )

-- One-wing stroke sweep at hover and at 8 m/s: total force on the right wing
-- at fixed flap angles, stroking down (rate<0) or up (rate>0). Negative stroke
-- angle puts the wing below horizontal (anhedral pose). Prints force components
-- (body frame: +x forward, +z up), mean separation and mean alpha.
main :: IO ()
main = do
  let hover = VehicleInput (1/240) (Vec3 0 0 0) zeroVec 0 0 0 0 1000 (Vec3 0 0 1)
      cruise = hover { bodyVelocityMS = Vec3 8 0 0 }
      measure input angle rate =
        let rs = wingLoadsWithStroke True 1 angle rate rate input kestrelModel 0 initialStrips
            f  = foldl' (\a r -> addVec a (resultForce r)) zeroVec rs
            sep = maximum (map (stripSeparation . resultState) rs)
            meanAlpha = sum (map (stripPreviousAlpha . resultState) rs)
                      / fromIntegral (length rs)
        in (f, sep, meanAlpha)
      row tag input angle rate =
        let (f, sep, a) = measure input angle rate
        in tag
           ++ " stroke=" ++ show (angle*180/pi)
           ++ "deg rate=" ++ show rate
           ++ " Fx=" ++ show (vx f)
           ++ " Fz=" ++ show (vz f)
           ++ " |F|=" ++ show (magnitude f)
           ++ " sep=" ++ show sep
           ++ " meanAlphaDeg=" ++ show (a*180/pi)
  putStrLn "== HOVER =="
  mapM_ (putStrLn . (\a -> row "H" hover a (-6))) [-0.55, -0.35, 0, 0.35]
  mapM_ (putStrLn . (\a -> row "H" hover a 6))    [-0.55, -0.35, 0, 0.35]
  putStrLn "== CRUISE 8 m/s =="
  mapM_ (putStrLn . (\a -> row "C" cruise a (-6))) [-0.55, -0.35, 0, 0.35]
  mapM_ (putStrLn . (\a -> row "C" cruise a 6))    [-0.55, -0.35, 0, 0.35]
