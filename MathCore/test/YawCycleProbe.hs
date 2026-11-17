{-# LANGUAGE OverloadedStrings #-}
module Main (main) where

import Data.List (foldl')
import qualified Numeric

import Born2Flap.Math.Types ( Vec3(..) )
import Born2Flap.Math.Vehicle
  ( addVec, scaleVec, zeroVec
  , VehicleInput(..), StripState(..), StripResult(..), initialStrips
  , prepareWingSystem, wingInitialRates, wingTrialMotion
  , wingElasticResidual )
import Born2Flap.Math.Model ( kestrelModel, mkWingModel, WingModel(..) )
import Born2Flap.Math.Planform ( wsSpanM, spanChord, spanTwist, spanDihed, shapeAspectRatio )
import Born2Flap.Math.Structure ( SparInventory(..), kestrelSpars )
import Born2Flap.Math.RollDynamics ( solveImplicitSystem )

stiffModel :: WingModel
stiffModel = mkWingModel "kestrel-stiff" (wmWing kestrelModel)
  (kestrelSpars { siBendScale = 3.6, siTwistScale = 8.4 }) 0.050

softModel :: WingModel
softModel = mkWingModel "kestrel-soft" (wmWing kestrelModel)
  (kestrelSpars { siBendScale = 3.6, siTwistScale = 2.1 }) 0.050

dt :: Double
dt = 1.0 / 240.0

f3 :: Double -> String
f3 v = Numeric.showFFloat (Just 3) v ""

fmtVec :: Vec3 -> String
fmtVec (Vec3 a b c) = "(" ++ f3 a ++ "," ++ f3 b ++ "," ++ f3 c ++ ")"

-- One implicit wing step (elastic coords solved like the production path),
-- with the body roll rate p fed through the strip section velocity.
wingStep :: VehicleInput -> Double -> Double -> Double -> Double -> [StripState] -> [StripResult]
wingStep = wingStepModel kestrelModel

wingStepModel :: WingModel -> VehicleInput -> Double -> Double -> Double -> Double -> [StripState] -> [StripResult]
wingStepModel model input p side stroke rate states =
  let system = prepareWingSystem True side stroke rate input model 0 0 states
      (vb,vt) = wingInitialRates system
      trial (b,t) = wingTrialMotion system (Vec3 p 0 0) zeroVec rate rate (b,t)
      residual [b,t] = let (rb,rt) = wingElasticResidual system (b,t) (trial (b,t)) in [rb,rt]
      residual _ = error "elastic coordinate count"
  in case solveImplicitSystem residual [vb,vt] of
       Just [b,t] -> trial (b,t)
       _          -> error "elastic solve did not converge"

-- Cycle mean of the per-wing force budget at the given stroke amplitude.
cycleBudget :: Double -> Double -> Double -> Double -> IO (Vec3, Double, Double, Double, Vec3, Double)
cycleBudget vx side amp freq = do
  let input = VehicleInput dt (Vec3 vx 0 0) zeroVec 0 0 0 0 1000 (Vec3 0 0 1)
      stepsPerCycle = max 4 (round (1.0 / (freq * dt))) :: Int
      runCycles n states sums =
        let phase t = amp * sin (2 * pi * freq * t)
            rate t = amp * 2 * pi * freq * cos (2 * pi * freq * t)
            loop i t st acc | i <= 0 = (st, acc)
                            | otherwise =
              let rs = wingStep input 0 side (phase t) (rate t) st
                  st' = map resultState rs
                  f = foldr (addVec . resultForce) zeroVec rs
                  m = foldr (addVec . resultMoment) zeroVec rs
                  liftX = sum [x (stripLift s) | s <- st']
                  dragX = sum [x (stripDrag s) | s <- st']
                  presX = sum [x (stripPressure s) | s <- st']
                  down = rate t < 0
                  a1 (f0,_,_,_,_,_,_) = f0
                  a2 (_,l,_,_,_,_,_) = l
                  a3 (_,_,d,_,_,_,_) = d
                  a4 (_,_,_,p,_,_,_) = p
                  a5 (_,_,_,_,m,_,_) = m
                  a6 (_,_,_,_,_,d0,_) = d0
                  a7 (_,_,_,_,_,_,n) = n
                  acc' = (addVec f (a1 acc), liftX + a2 acc, dragX + a3 acc
                         , presX + a4 acc, addVec m (a5 acc)
                         , if down then addVec f (a6 acc) else a6 acc
                         , if down then 1 + a7 acc else a7 acc)
              in loop (i-1) (t+dt) st' acc'
            (stFinal, accFinal) = loop stepsPerCycle (fromIntegral (n-1) / freq) states sums
        in if n <= 1 then (stFinal, accFinal)
           else runCycles (n-1) stFinal accFinal
      (_, (fSum, liftSum, dragSum, presSum, mSum, downF, downN)) =
        runCycles 3 initialStrips (zeroVec,0,0,0,zeroVec,zeroVec,0)
      nSteps = fromIntegral stepsPerCycle :: Double
      downMean = scaleVec (1 / max 1 downN) downF
  putStrLn $ "  vx=" ++ f3 vx ++ " side=" ++ show side ++ " amp=" ++ f3 (amp*180/pi) ++ "deg freq=" ++ f3 freq
    ++ "\n    mean F     = " ++ show (scaleVec (1/nSteps) fSum)
    ++ "\n    liftPart.x = " ++ show (liftSum/nSteps)
    ++ "\n    drag.x     = " ++ show (dragSum/nSteps)
    ++ "\n    pressure.x = " ++ show (presSum/nSteps)
    ++ "\n    check Fx-(lift+drag+pres) = " ++ show ((x (scaleVec (1/nSteps) fSum)) - (liftSum+dragSum+presSum)/nSteps)
    ++ "\n    mean M     = " ++ show (scaleVec (1/nSteps) mSum)
    ++ "\n    downstroke mean F = " ++ show downMean
  pure (scaleVec (1/nSteps) fSum, liftSum/nSteps, dragSum/nSteps, presSum/nSteps
       , scaleVec (1/nSteps) mSum, x downMean)

debugSteps :: IO ()
debugSteps = do
  let input = VehicleInput dt (Vec3 6 0 0) zeroVec 0 0 0 0 1000 (Vec3 0 0 1)
      amp = 14.4 * pi / 180
      freq = 2.5
      loop i t st | i <= 0 = pure ()
                  | otherwise = do
        let rate = amp * 2 * pi * freq * cos (2 * pi * freq * t)
            rs = wingStep input 0 (-1) (amp * sin (2 * pi * freq * t)) rate st
            st' = map resultState rs
            fx = sum [x (resultForce r) | r <- rs]
            lx = sum [x (stripLift s) | s <- st']
            dx = sum [x (stripDrag s) | s <- st']
            px = sum [x (stripPressure s) | s <- st']
        putStrLn $ "step t=" ++ f3 t ++ " Fx=" ++ show fx ++ " lift=" ++ show lx
          ++ " drag=" ++ show dx ++ " pres=" ++ show px ++ " sum=" ++ show (lx+dx+px)
          ++ " residual=" ++ show (fx - (lx+dx+px))
        loop (i-1) (t+dt) st'
  loop 8 0 initialStrips

-- Closed roll loop: the body roll rate p integrates from the wings' roll
-- moment at the given roll inertia (the sim's roll DOF in miniature).
closedRoll :: Double -> Double -> Double -> Double -> Double -> IO ()
closedRoll vx ixD aBig aSmall freq = do
  let input = VehicleInput dt (Vec3 vx 0 0) zeroVec 0 0 0 0 1000 (Vec3 0 0 1)
      stepsPerCycle = max 4 (round (1.0 / (freq * dt)))
      measureCycles = 2
      warmCycles = 3
      totalSteps = stepsPerCycle * (warmCycles + measureCycles)
      loop i t p stL stR sx sz smx | i <= 0 = (sx, sz, smx, p)
                                   | otherwise =
        let sL = aBig * sin (2*pi*freq*t); rL = aBig * 2*pi*freq*cos (2*pi*freq*t)
            sR = aSmall * sin (2*pi*freq*t); rR = aSmall * 2*pi*freq*cos (2*pi*freq*t)
            rsL = wingStep input p (-1) sL rL stL
            rsR = wingStep input p 1 sR rR stR
            fx = sum [x (resultForce r) | r <- rsL ++ rsR]
            mz = sum [z (resultMoment r) | r <- rsL ++ rsR]
            mx = sum [x (resultMoment r) | r <- rsL ++ rsR]
            p' = p + (mx / ixD) * dt
            measuring = i <= stepsPerCycle * measureCycles
        in loop (i-1) (t+dt) p' (map resultState rsL) (map resultState rsR)
             (if measuring then sx+fx else sx)
             (if measuring then sz+mz else sz)
             (if measuring then smx+mx else smx)
      (sx, sz, smx, pEnd) = loop totalSteps 0 0 initialStrips initialStrips 0 0 0
      n = fromIntegral (stepsPerCycle * measureCycles)
  putStrLn $ "  rollInertia=" ++ f3 ixD ++ " kg*m^2"
    ++ "\n    cycle-mean total Fx (both wings) = " ++ f3 (sx/n)
    ++ "\n    cycle-mean wing yaw moment Mz = " ++ f3 (sz/n)
    ++ "\n    cycle-mean wing roll moment Mx = " ++ f3 (smx/n)
    ++ "\n    final roll rate p = " ++ f3 pEnd

-- Per-phase budget: downstroke vs upstroke mean vertical force and tip twist.
phaseBudget :: WingModel -> Double -> Double -> Double -> IO ()
phaseBudget model vx amp freq = do
  let input = VehicleInput dt (Vec3 vx 0 0) zeroVec 0 0 0 0 1000 (Vec3 0 0 1)
      stepsPerCycle = max 4 (round (1.0 / (freq * dt)))
      loop i t st sx dz dzN uz uzN twD twN twU twUN | i <= 0 = (sx,dz,dzN,uz,uzN,twD,twN,twU,twUN)
                                                    | otherwise =
        let s = amp * sin (2*pi*freq*t); r = amp * 2*pi*freq*cos (2*pi*freq*t)
            rs = wingStepModel model input 0 (-1) s r st
            st' = map resultState rs
            fz = sum [z (resultForce r') | r' <- rs]
            fx = sum [x (resultForce r') | r' <- rs]
            tw = stripTwistAero (last st')
            down = r < 0
        in loop (i-1) (t+dt) st' (sx+fx)
             (if down then dz+fz else dz) (if down then dzN+1 else dzN)
             (if down then uz else uz+fz) (if down then uzN else uzN+1)
             (if down then twD+tw else twD) (if down then twN+1 else twN)
             (if down then twU else twU+tw) (if down then twUN else twUN+1)
      (sx,dz,dzN,uz,uzN,twD,twN,twU,twUN) = loop (stepsPerCycle*3) 0 initialStrips 0 0 0 0 0 0 0 0 0
      nSteps = fromIntegral (stepsPerCycle*3)
  putStrLn $ "  vx=" ++ f3 vx ++ " amp=" ++ f3 (amp*180/pi) ++ "deg freq=" ++ f3 freq
    ++ "\n    mean Fx = " ++ f3 (sx/nSteps)
    ++ "\n    downstroke mean Fz = " ++ f3 (dz / fromIntegral dzN)
    ++ "\n    upstroke   mean Fz = " ++ f3 (uz / fromIntegral uzN)
    ++ "\n    downstroke mean tip twist = " ++ f3 (twD / fromIntegral twN * 180 / pi) ++ " deg"
    ++ "\n    upstroke   mean tip twist = " ++ f3 (twU / fromIntegral twUN * 180 / pi) ++ " deg"

phaseTrace :: IO ()
phaseTrace = do
  let input = VehicleInput dt (Vec3 6 0 0) zeroVec 0 0 0 0 1000 (Vec3 0 0 1)
      ixD = 0.00074484
      aBig = 20.16*pi/180; aSmall = 8.64*pi/180
      freq = 2.5
      stepsPerCycle = round (1.0/(freq*dt))
      loop i t p stL stR | i <= 0 = pure ()
                         | otherwise = do
        let sL = aBig * sin (2*pi*freq*t); rL = aBig * 2*pi*freq*cos (2*pi*freq*t)
            sR = aSmall * sin (2*pi*freq*t); rR = aSmall * 2*pi*freq*cos (2*pi*freq*t)
            rsL = wingStep input p (-1) sL rL stL
            rsR = wingStep input p 1 sR rR stR
            fx = sum [x (resultForce r) | r <- rsL ++ rsR]
            mz = sum [z (resultMoment r) | r <- rsL ++ rsR]
            mx = sum [x (resultMoment r) | r <- rsL ++ rsR]
            fzL = sum [z (resultForce r) | r <- rsL]
            fzR = sum [z (resultForce r) | r <- rsR]
            p' = p + (mx / ixD) * dt
        putStrLn $ "t=" ++ f3 t ++ " sL=" ++ f3 (sL*180/pi) ++ " rL=" ++ f3 rL
          ++ " p=" ++ f3 p ++ " Mx=" ++ f3 mx ++ " FzL=" ++ f3 fzL ++ " FzR=" ++ f3 fzR
          ++ " Fx=" ++ f3 fx ++ " Mz=" ++ f3 mz
        loop (i-1) (t+dt) p' (map resultState rsL) (map resultState rsR)
  loop (stepsPerCycle `div` 4) 0 0 initialStrips initialStrips

main :: IO ()
main = do
  phaseTrace
  debugSteps
  let stations = wmStations kestrelModel
      dr = wsSpanM (wmWing kestrelModel) / fromIntegral (length stations)
      area = sum [spanChord st * dr | st <- stations]
  putStrLn $ "kestrel wing: halfSpan=" ++ f3 (wsSpanM (wmWing kestrelModel))
    ++ " strips=" ++ show (length stations) ++ " area=" ++ show area
    ++ " AR=" ++ show (shapeAspectRatio (wmWing kestrelModel))
  putStrLn $ "geometric twist deg (root->tip): " ++ show [spanTwist st | st <- stations]
  putStrLn $ "geometric dihedral deg: " ++ show [spanDihed st | st <- stations]
  putStrLn $ "chord (root->tip): " ++ show [spanChord st | st <- stations]
  putStrLn "=== PHASE BUDGET (big wing 20.16 deg) ==="
  phaseBudget kestrelModel 6 (20.16*pi/180) 2.5
  putStrLn "=== AMPLITUDE SWEEP (kestrel, 6 m/s, 2.5 Hz, big wing Fx) ==="
  mapM_ (\a -> phaseBudget kestrelModel 6 (a*pi/180) 2.5) [8.64, 14.4, 20.16, 26.0, 32.0]
  putStrLn "=== TWIST STIFFNESS VARIANTS (20.16 deg) ==="
  putStrLn "-- stiff (twistScale 8.4):"
  phaseBudget stiffModel 6 (20.16*pi/180) 2.5
  putStrLn "-- default (twistScale 4.2):"
  phaseBudget kestrelModel 6 (20.16*pi/180) 2.5
  putStrLn "-- soft (twistScale 2.1):"
  phaseBudget softModel 6 (20.16*pi/180) 2.5
  putStrLn "=== CLOSED ROLL (differential, 6 m/s, varying roll inertia) ==="
  let aBig = 20.16*pi/180; aSmall = 8.64*pi/180
  closedRoll 6 0.00074484 aBig aSmall 2.5
  closedRoll 6 0.00148968 aBig aSmall 2.5
  closedRoll 6 0.00297936 aBig aSmall 2.5
  closedRoll 6 0.00595872 aBig aSmall 2.5
  putStrLn "=== SYMMETRIC (neutral flap 14.4 deg, both wings) ==="
  let aSym = 14.4 * pi / 180
  (fL,_,_,_,_,_) <- cycleBudget 6 (-1) aSym 2.5
  (fR,_,_,_,_,_) <- cycleBudget 6 1 aSym 2.5
  putStrLn $ "  sum Fx = " ++ f3 (x fL + x fR) ++ " N (cruise thrust-drag balance)"
  putStrLn "=== DIFFERENTIAL (yaw stick right: L=20.16deg, R=8.64deg) ==="
  (fl,_,_,_,ml,_) <- cycleBudget 6 (-1) aBig 2.5
  (fr,_,_,_,mr,_) <- cycleBudget 6 1 aSmall 2.5
  putStrLn $ "  BIG wing (L) mean Fx = " ++ f3 (x fl)
  putStrLn $ "  small wing (R) mean Fx = " ++ f3 (x fr)
  putStrLn $ "  delta Fx (big - small) = " ++ f3 (x fl - x fr)
  putStrLn $ "  yaw moment from wings (Mz) = " ++ f3 (z ml + z mr)
  putStrLn $ "  roll moment from wings (Mx) = " ++ f3 (x ml + x mr)
  putStrLn "=== DIFFERENTIAL at 8 m/s ==="
  (fl8,_,_,_,ml8,_) <- cycleBudget 8 (-1) aBig 2.5
  (fr8,_,_,_,mr8,_) <- cycleBudget 8 1 aSmall 2.5
  putStrLn $ "  BIG wing mean Fx = " ++ f3 (x fl8) ++ ", small = " ++ f3 (x fr8)
  putStrLn $ "  delta Fx = " ++ f3 (x fl8 - x fr8) ++ ", Mz = " ++ f3 (z ml8 + z mr8)