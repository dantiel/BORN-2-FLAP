-- Diagnostic, not a flight certification test. Run from MathCore:
-- cabal exec -- runghc -isrc test/RollCouplingAudit.hs
module Main (main) where

import Text.Printf (printf)
import Born2Flap.Math.Planform
import Born2Flap.Math.Vehicle (airDensity)
import Born2Flap.Math.Servo

main :: IO ()
main = do
  let structural = shapeFlapInertia referenceWingMassKg 0 defaultBirdWing
      wing = shapeFlapInertia referenceWingMassKg airDensity defaultBirdWing
      -- Reference assumptions: rounded runtime inertia logged for the 450 g
      -- craft, and reflected rotor inertia currently hardcoded in stepServo.
      lockedRoll = 0.008
      rotor = 0.0016
      joint = wing + rotor
      torque = servoStallTorqueNm defaultServoSpec
      fixedBase = torque / joint
      freeBase = torque / (joint - 2 * wing * wing / lockedRoll)
      bodyAcceleration = -2 * wing * freeBase / lockedRoll
      dt = 1 / 240
      (_, servo) = stepServo defaultServoSpec defaultBatterySpec 40 0 wing
                     (batteryNominalVoltage defaultBatterySpec) dt defaultServoState
      measured = servoRateDegPerSec servo * pi / 180 / dt
      -- Equal/opposite servo torques, no external moment. Right q increases,
      -- left q decreases. These are the three coupled roll equations.
      bodyResidual = lockedRoll * bodyAcceleration + 2 * wing * freeBase
      jointResidual = wing * bodyAcceleration + joint * freeBase - torque
  printf "Reference wing structural inertia: %.9f kg m^2\n" structural
  printf "Reference wing including modeled added air: %.9f kg m^2\n" wing
  printf "Legacy controller reference / fixed-base prediction: %.3f / %.3f rad/s^2\n" measured fixedBase
  printf "Coupled differential acceleration, same motor torque: %.3f rad/s^2\n" freeBase
  printf "Coupled body acceleration: %.3f rad/s^2\n" bodyAcceleration
  printf "Coupled body / differential joint motion ratio: %.3f\n" (2 * wing / lockedRoll)
  printf "Analytical body/joint equation residuals: %.3g / %.3g Nm\n" bodyResidual jointResidual
  if abs (measured - fixedBase) < 1e-8 && abs bodyResidual < 1e-10 && abs jointResidual < 1e-10
    then putStrLn "REFERENCE CONFIRMED: runtime now uses RollDynamics for actual joint/body integration."
    else fail "Audit assumptions changed; inspect model before interpreting these numbers."
