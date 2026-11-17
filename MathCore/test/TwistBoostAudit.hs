{-# LANGUAGE OverloadedStrings #-}
module Main (main) where

import Numeric

import Born2Flap.Math.Types ( Vec3(..) )
import Born2Flap.Math.Vehicle
  ( addVec, scaleVec, zeroVec
  , VehicleInput(..), StripState(..), StripResult(..), initialStrips
  , prepareWingSystem, wingInitialRates, wingTrialMotion
  , wingElasticResidual )
import Born2Flap.Math.Model ( kestrelModel, mkWingModel, WingModel(..) )
import Born2Flap.Math.Structure ( SparInventory(..), kestrelSpars )
import Born2Flap.Math.RollDynamics ( solveImplicitSystem )
import Born2Flap.Math.FirmwareVehicle ( kestrelTailLoads, TailField(..) )

vx_, vy_, vz_ :: Vec3 -> Double
vx_ (Vec3 a _ _) = a
vy_ (Vec3 _ b _) = b
vz_ (Vec3 _ _ c) = c

dt :: Double
dt = 1.0 / 240.0

freq :: Double
freq = 2.5

f3 :: Double -> String
f3 v = showFFloat (Just 4) v ""

mkModel :: Double -> WingModel
mkModel ts = mkWingModel "kestrel-twist" (wmWing kestrelModel)
  (kestrelSpars { siBendScale = 3.6, siTwistScale = ts }) 0.050

-- One implicit wing step with the body roll rate p fed through the strip
-- section velocity (same seam the production solver uses).
wingStepM :: WingModel -> VehicleInput -> Double -> Double -> Double -> Double -> [StripState] -> [StripResult]
wingStepM model input p side stroke rate states =
  let system = prepareWingSystem True side stroke rate input model 0 0 states
      (vb,vt) = wingInitialRates system
      trial (b,t) = wingTrialMotion system (Vec3 p 0 0) zeroVec rate rate (b,t)
      residual [b,t] = let (rb,rt) = wingElasticResidual system (b,t) (trial (b,t)) in [rb,rt]
      residual _ = error "elastic coordinate count"
  in case solveImplicitSystem residual [vb,vt] of
       Just [b,t] -> trial (b,t)
       _          -> error "elastic solve did not converge"

-- Cycle budget of one wing at fixed airspeed, body roll rate p and stroke
-- amplitude, split into downstroke (rate<0) / upstroke halves.
data WingBudget = WingBudget
  { bFx, bFz, bFxD, bFzD, bTwD, bTwU, bND, bNU, bNT :: Double }

budgetP :: WingModel -> Double -> Double -> Double -> Double -> WingBudget
budgetP model vx0 p side amp =
  let input = VehicleInput dt (Vec3 vx0 0 0) zeroVec 0 0 0 0 1000 (Vec3 0 0 1)
      stepsPerCycle = max 4 (round (1.0 / (freq * dt))) :: Int
      phase t = amp * sin (2 * pi * freq * t)
      rate t = amp * 2 * pi * freq * cos (2 * pi * freq * t)
      go i t st fx fz fxd fzd twd twu nd nu | i <= 0 =
        WingBudget fx fz fxd fzd twd twu nd nu (fromIntegral (stepsPerCycle*3))
        | otherwise =
        let r = rate t
            rs = wingStepM model input p side (phase t) r st
            st' = map resultState rs
            fx' = sum [vx_ (resultForce r') | r' <- rs]
            fz' = sum [vz_ (resultForce r') | r' <- rs]
            tw = stripTwistAero (last st')
            down = r < 0
        in go (i-1) (t+dt) st'
             (fx+fx') (fz+fz')
             (if down then fxd+fx' else fxd) (if down then fzd+fz' else fzd)
             (if down then twd+tw else twd) (if down then twu else twu+tw)
             (if down then nd+1 else nd) (if down then nu else nu+1)
  in go (stepsPerCycle*3) 0 initialStrips 0 0 0 0 0 0 0 0

partA :: IO ()
partA = do
  putStrLn "=== A: twist-scale sweep at differential amplitude (L=20.16deg, R=8.64deg, 6 m/s, 2.5 Hz) ==="
  putStrLn "  ts     FxBig     FxSmall   dFx       FxBigDown FxSmDown  twBigD(deg) twBigU(deg)"
  mapM_ runA [2.1, 4.2, 8.4, 12.6, 16.8, 25.2]
  where
    runA ts = do
      let m = mkModel ts
          b = budgetP m 6 0 (-1) (20.16*pi/180)
          s = budgetP m 6 0 1 (8.64*pi/180)
          nt = bNT b
          rad = 180/pi
      putStrLn $ "  " ++ f3 ts
        ++ "  " ++ f3 (bFx b/nt) ++ "  " ++ f3 (bFx s/nt) ++ "  " ++ f3 ((bFx b - bFx s)/nt)
        ++ "  " ++ f3 (bFxD b/bND b) ++ "  " ++ f3 (bFxD s/bND s)
        ++ "  " ++ f3 (bTwD b/bND b*rad) ++ "  " ++ f3 (bTwU b/bNU b*rad)

partB :: IO ()
partB = do
  putStrLn "=== B: roll-rate coupling on the BIG wing (20.16deg), twist 4.2 vs 8.4 ==="
  putStrLn "  ts    p(rad/s)  FxBig     FxBigDown  FzBigDown"
  mapM_ runB [(4.2, -1.0), (4.2, -0.5), (4.2, 0), (4.2, 0.5), (4.2, 1.0)
             ,(8.4, -1.0), (8.4, -0.5), (8.4, 0), (8.4, 0.5), (8.4, 1.0)]
  where
    runB (ts,p) = do
      let m = mkModel ts
          b = budgetP m 6 p (-1) (20.16*pi/180)
      putStrLn $ "  " ++ f3 ts ++ "  " ++ f3 p
        ++ "  " ++ f3 (bFx b/bNT b) ++ "  " ++ f3 (bFxD b/bND b) ++ "  " ++ f3 (bFzD b/bND b)

tailForceOf :: TailField -> Vec3
tailForceOf f = addVec (tfLift f) (addVec (tfPressure f) (tfDrag f))

-- Flat fan heaved by a stroke-locked body roll oscillation: does it slide on
-- one half-cycle and catch on the other? Split the cycle-mean by p>0 / p<0.
partC :: IO ()
partC = do
  putStrLn "=== C: flat fan heave from body roll p(t)=1.0*sin(2*pi*2.5*t), vx=6 m/s ==="
  putStrLn "  inc(deg)  meanFx     meanFz     meanMx    |  Fx(p>0)   Fx(p<0)   Fz(p>0)   Fz(p<0)"
  mapM_ runC [-2.3, 4, 8, 10, 13, 16]
  where
    runC inc = do
      let stepsPerCycle = max 4 (round (1.0 / (freq * dt))) :: Int
          fields t = kestrelTailLoads inc (Vec3 6 0 0) (Vec3 (1.0*sin (2*pi*freq*t)) 0 0)
          go i t sf sm sxp sxn szp szn np nn | i <= 0 = (sf,sm,sxp,sxn,szp,szn,np,nn)
                                             | otherwise =
            let fs = fields t
                f = foldr (addVec . tailForceOf) zeroVec fs
                m = foldr (addVec . tfMoment) zeroVec fs
                pos = sin (2*pi*freq*t) > 0
            in go (i-1) (t+dt)
                 (addVec sf f) (addVec sm m)
                 (if pos then vx_ f + sxp else sxp) (if pos then sxn else vx_ f + sxn)
                 (if pos then vz_ f + szp else szp) (if pos then szn else vz_ f + szn)
                 (if pos then np+1 else np) (if pos then nn else nn+1)
          (sf,sm,sxp,sxn,szp,szn,np,nn) = go (stepsPerCycle*3) 0 zeroVec zeroVec 0 0 0 0 0 0
          nt = fromIntegral (stepsPerCycle*3)
      putStrLn $ "  " ++ f3 inc
        ++ "  " ++ f3 (vx_ sf/nt) ++ "  " ++ f3 (vz_ sf/nt) ++ "  " ++ f3 (vx_ sm/nt)
        ++ "  |  " ++ f3 (sxp/max 1 np) ++ "  " ++ f3 (sxn/max 1 nn)
        ++ "  " ++ f3 (szp/max 1 np) ++ "  " ++ f3 (szn/max 1 nn)

main :: IO ()
main = do
  partA
  partB
  partC