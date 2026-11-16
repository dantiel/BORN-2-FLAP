module Main (main) where

import Text.Printf (printf)
import Control.Monad (when)

import Born2Flap.Math.Types ( Vec3(..) )
import Born2Flap.Math.Vehicle
import Born2Flap.Math.Model ( kestrelModel )

-- Per-strip pitch-lever-arm decomposition, cruise 8 m/s, downstroke.
-- Body frame: +x forward, +z up, +Y moment rotates the nose DOWN (code convention).
-- my = rz*Fx - rx*Fz  =  thrust-below-CG term + quarter-chord lift term.
-- A deeper downstroke puts the AC further below the CG (rz < 0): the thrust term
-- rz*Fx grows with anhedral depth — the lever arm the user asks about.
main :: IO ()
main = do
  let hover = VehicleInput (1/240) (Vec3 0 0 0) zeroVec 0 0 0 0 1000 (Vec3 0 0 1)
      cruise = hover { bodyVelocityMS = Vec3 8 0 0 }
      measure input (angle, rate) detailed =
        let rs = wingLoadsWithStroke True 1 angle rate rate input kestrelModel 0 initialStrips
            pitch r = let pos = stripPosition (resultState r)
                          f   = resultForce r
                          l   = resultLift r
                      in (vz pos * vx f - vx pos * vz f, vz pos * vx l - vx pos * vz l)
            per  = zipWith (\i r -> (i, pitch r)) [0..] rs
            sumP = foldl (\a (_,(m,_)) -> a+m) 0 per
            sumL = foldl (\a (_,(_,m)) -> a+m) 0 per
            tip = let r = last rs
                      pos = stripPosition (resultState r)
                      f = resultForce r
                  in (vz pos, vx f, vz f, vz pos * vx f - vx pos * vz f)
            root = let r = head rs
                       pos = stripPosition (resultState r)
                       f = resultForce r
                   in (vz pos, vx f, vz f, vz pos * vx f - vx pos * vz f)
            fsum = foldl (\a r -> addVec a (resultForce r)) zeroVec rs
            line = ( "stroke=" ++ fmt4 (angle*180/pi) ++ "deg rate=" ++ fmt4 rate
             ++ "  SUM my(rxF)=" ++ fmt5 sumP ++ " Nm   (liftPart share " ++ fmt5 sumL ++ ")"
             ++ "  SigmaFx=" ++ fmt5 (vx fsum) ++ " SigmaFz=" ++ fmt5 (vz fsum)
             ++ "  tip: z=" ++ fmt4 (case tip of (a,_,_,_) -> a)
             ++ " m Fx=" ++ fmt4 (case tip of (_,a,_,_) -> a)
             ++ " Fz=" ++ fmt4 (case tip of (_,_,a,_) -> a)
             ++ " my=" ++ fmt5 (case tip of (_,_,_,a) -> a)
             ++ "  root: z=" ++ fmt4 (case root of (a,_,_,_) -> a)
             ++ " Fx=" ++ fmt4 (case root of (_,a,_,_) -> a)
             ++ " Fz=" ++ fmt4 (case root of (_,_,a,_) -> a)
             ++ " my=" ++ fmt5 (case root of (_,_,_,a) -> a) )
        in do putStrLn line
              when detailed $
                mapM_ (\(i,(m,_)) -> putStrLn ("    strip " ++ show i ++ "  my=" ++ fmt5 m)) per
  putStrLn "== CRUISE 8 m/s, downstroke rate -6 rad/s (partial throttle) =="
  putStrLn "(my>0 = nose-down, my<0 = nose-up in this convention)"
  mapM_ (\p -> measure cruise p False) [(-0.15,-6), (-0.35,-6), (-0.55,-6)]
  putStrLn ""
  putStrLn "== CRUISE 8 m/s, downstroke rate -18 rad/s (full-throttle mid-stroke) =="
  mapM_ (\p -> measure cruise p False) [(-0.15,-18), (-0.35,-18), (-0.55,-18)]
  putStrLn ""
  putStrLn "== per-strip detail, rate -18, deepest downstroke =="
  measure cruise (-0.55,-18) True
  where fmt4 v = printf "%9.4f" (v :: Double)
        fmt5 v = printf "%10.5f" (v :: Double)