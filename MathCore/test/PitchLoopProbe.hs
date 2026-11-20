{-# LANGUAGE OverloadedStrings #-}
module Main (main) where

import Numeric

import Born2Flap.Math.Types ( Vec3(..) )
import Born2Flap.Math.Vehicle
  ( addVec, scaleVec, zeroVec, crossVec
  , VehicleInput(..), StripState(..), StripResult(..), initialStrips
  , prepareWingSystem, wingInitialRates, wingTrialMotion
  , wingElasticResidual )
import Born2Flap.Math.Model ( kestrelModel, WingModel(..) )
import Born2Flap.Math.RollDynamics ( solveImplicitSystem )

vx_, vy_, vz_ :: Vec3 -> Double
vx_ (Vec3 a _ _) = a
vy_ (Vec3 _ b _) = b
vz_ (Vec3 _ _ c) = c

dt :: Double
dt = 1.0 / 240.0

freq :: Double
freq = 2.5

-- One implicit wing step (production kestrel model, no roll rate), both wings.
wingStepM :: WingModel -> VehicleInput -> Double -> Double -> Double -> [StripState] -> [StripResult]
wingStepM model input side stroke rate states =
  let system = prepareWingSystem True side stroke rate input model 0 0 states
      (vb,vt) = wingInitialRates system
      trial (b,t) = wingTrialMotion system (Vec3 0 0 0) zeroVec rate rate (b,t)
      residual [b,t] = let (rb,rt) = wingElasticResidual system (b,t) (trial (b,t)) in [rb,rt]
      residual _ = error "elastic coordinate count"
  in case solveImplicitSystem residual [vb,vt] of
       Just [b,t] -> trial (b,t)
       _          -> error "elastic solve did not converge"

-- Cycle totals of one wing, split downstroke (anhedral, stroke<0) / upstroke.
-- fX/fZ = total aerodynamic force; mY = r x F pitch moment; lift-only rows
-- isolate the circulation lift at its deformed strip position, so the gap
-- between total and lift-only is exactly what drag+pressure+couple eat.
data Cycle = Cycle
  { cFxD, cFzD, cMyD, cFxU, cFzU, cMyU :: Double
  , cLfFxD, cLfFzD, cLfMyD, cLfFxU, cLfFzU, cLfMyU :: Double
  }

budget :: WingModel -> Double -> Double -> Double -> Cycle
budget model vx0 amp bias =
  let input = VehicleInput dt (Vec3 vx0 0 0) zeroVec 0 0 0 0 1000 (Vec3 0 0 1)
      stepsPerCycle = max 4 (round (1.0 / (freq * dt))) :: Int
      phase t = bias + amp * sin (2 * pi * freq * t)
      rate t = amp * 2 * pi * freq * cos (2 * pi * freq * t)
      go i t st acc | i <= 0 = acc
        | otherwise =
        let r = rate t
            rs = wingStepM model input (-1) (phase t) r st
            st' = map resultState rs
            fx = sum [vx_ (resultForce x) | x <- rs]
            fz = sum [vz_ (resultForce x) | x <- rs]
            my = sum [vy_ (resultMoment x) | x <- rs]
            lfx = sum [vx_ (resultLift x) | x <- rs]
            lfz = sum [vz_ (resultLift x) | x <- rs]
            lmy = sum [vy_ (crossVec (stripPosition s) (resultLift x)) | (s,x) <- zip st' rs]
            down = r < 0
            add a = if down
              then a { cFxD = cFxD a + fx, cFzD = cFzD a + fz, cMyD = cMyD a + my
                     , cLfFxD = cLfFxD a + lfx, cLfFzD = cLfFzD a + lfz, cLfMyD = cLfMyD a + lmy }
              else a { cFxU = cFxU a + fx, cFzU = cFzU a + fz, cMyU = cMyU a + my
                     , cLfFxU = cLfFxU a + lfx, cLfFzU = cLfFzU a + lfz, cLfMyU = cLfMyU a + lmy }
        in go (i-1) (t+dt) st' (add acc)
  in go (stepsPerCycle*3) 0 initialStrips (Cycle 0 0 0 0 0 0 0 0 0 0 0 0)

-- Cycle-mean totals (both halves) for a stroke-CENTER bias — the real
-- maneuver condition: flap while HOLDING the stroke centre in anhedral
-- (bias < 0) or wings-up (bias > 0). The mY difference between the two
-- biases is the flap-position pitch authority the loop recovery rides on.
data Mean = Mean { mFx, mFz, mMy, mLfMy :: Double }

budgetMean :: WingModel -> Double -> Double -> Double -> Mean
budgetMean model vx0 amp bias =
  let c = budget model vx0 amp bias
      n = 288 :: Double
  in Mean ((cFxD c + cFxU c) / n) ((cFzD c + cFzU c) / n)
          ((cMyD c + cMyU c) / n) ((cLfMyD c + cLfMyU c) / n)

f3 :: Double -> String
f3 v = showFFloat (Just 4) v ""

runCase :: String -> Double -> Double -> IO ()
runCase label vx amp = do
  let c = budget kestrelModel vx amp 0
      n = 144 :: Double   -- 3 cycles x 48 steps/half
  putStrLn ("--- " ++ label ++ " (vx=" ++ f3 vx ++ " m/s, amp=" ++ f3 amp ++ " rad, one wing, per half-cycle mean, both wings = 2x mY) ---")
  putStrLn ("  DOWN (anhedral):  Fx=" ++ f3 (cFxD c / n) ++ "  Fz=" ++ f3 (cFzD c / n)
         ++ "  mY=" ++ f3 (cMyD c / n) ++ "   lift-only mY=" ++ f3 (cLfMyD c / n))
  putStrLn ("  UP   (dihedral):  Fx=" ++ f3 (cFxU c / n) ++ "  Fz=" ++ f3 (cFzU c / n)
         ++ "  mY=" ++ f3 (cMyU c / n) ++ "   lift-only mY=" ++ f3 (cLfMyU c / n))
  putStrLn ("  lift-only Fx DOWN/UP = " ++ f3 (cLfFxD c / n) ++ " / " ++ f3 (cLfFxU c / n)
         ++ "   lift-only Fz DOWN/UP = " ++ f3 (cLfFzD c / n) ++ " / " ++ f3 (cLfFzU c / n))

runBias :: String -> Double -> Double -> IO ()
runBias label vx amp = do
  let an = budgetMean kestrelModel vx amp (-0.5)
      up = budgetMean kestrelModel vx amp (0.5)
  putStrLn ("--- " ++ label ++ " BIAS (vx=" ++ f3 vx ++ " m/s, amp=" ++ f3 amp
         ++ " rad; cycle means, one wing, both wings = 2x) ---")
  putStrLn ("  anhedral-bias -0.5:  Fx=" ++ f3 (mFx an) ++ "  Fz=" ++ f3 (mFz an)
         ++ "  mY=" ++ f3 (mMy an) ++ "   lift-only mY=" ++ f3 (mLfMy an))
  putStrLn ("  wings-up-bias +0.5:  Fx=" ++ f3 (mFx up) ++ "  Fz=" ++ f3 (mFz up)
         ++ "  mY=" ++ f3 (mMy up) ++ "   lift-only mY=" ++ f3 (mLfMy up))
  putStrLn ("  authority dM = mY(an) - mY(up) = " ++ f3 (mMy an - mMy up)
         ++ "   (lift-only dM = " ++ f3 (mLfMy an - mLfMy up) ++ ")")

main :: IO ()
main = do
  putStrLn "=== pitch-loop probe: anhedral (downstroke) vs wings-up (upstroke) pitch moment ==="
  runCase "cruise" 8.0 0.55
  runCase "pull-up" 5.0 0.55
  runCase "loop-top" 3.0 0.7
  runBias "cruise" 8.0 0.45
  runBias "pull-up" 5.0 0.45
  runBias "loop-top" 3.0 0.45