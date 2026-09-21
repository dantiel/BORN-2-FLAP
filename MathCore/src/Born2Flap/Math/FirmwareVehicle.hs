{-# LANGUAGE DerivingStrategies #-}
{-# LANGUAGE StrictData #-}

-- | The full "simulation in the simulation": firmware mixer → servo actuator
-- → wing aerodynamics → aerodynamic hinge torque feeding back into the servo.
--
-- This is the loop the owner asked for: the firmware (a faithful port of
-- PteronautOS @_computeServoMixer@) reacts to RC pilot input; its wing-servo
-- commands are realised by a servo model that *struggles* against the real
-- aerodynamic hinge torque the wings produce; and the resulting actual flap
-- angle drives the wing physics. Servo, wing size, and battery are all
-- selectable so the same firmware flies (or stalls) exactly as it would in
-- the physical world.
module Born2Flap.Math.FirmwareVehicle
  ( FirmwareVehicleState(..)
  , defaultFirmwareVehicleState
  , stepFirmwareVehicle
  ) where

import Born2Flap.Math.Types (Vec3(..))
import Born2Flap.Math.Firmware
import Born2Flap.Math.Servo
import Born2Flap.Math.Vehicle
  ( VehicleInput(..), VehicleOutput(..)
  , StripState(..), StripResult(..), initialStrips
  , stepWingWithStroke
  , addVec, crossVec, scaleVec, magnitude, zeroVec, vy, radians )

data FirmwareVehicleState = FirmwareVehicleState
  { fvFirmware        :: !FirmwareState
  , fvServoLeft       :: !ServoState
  , fvServoRight      :: !ServoState
  , fvLeftStrips      :: ![StripState]
  , fvRightStrips     :: ![StripState]
  , fvLeftHingeTorqueNm  :: !Double   -- ^ aero load fed to the servo next step
  , fvRightHingeTorqueNm :: !Double
  , fvBatterySoc      :: !Double     -- ^ 0..1, drains under servo current
  } deriving stock (Eq, Show)

defaultFirmwareVehicleState :: FirmwareVehicleState
defaultFirmwareVehicleState = FirmwareVehicleState
  { fvFirmware = defaultFirmwareState
  , fvServoLeft = defaultServoState
  , fvServoRight = defaultServoState
  , fvLeftStrips = initialStrips
  , fvRightStrips = initialStrips
  , fvLeftHingeTorqueNm = 0
  , fvRightHingeTorqueNm = 0
  , fvBatterySoc = 1.0
  }

-- | One closed-loop timestep.
--
--   * @rc@       — CRSF radio channels (pilot input)
--   * @params@   — firmware mixer knobs (flight profile, servo speed, …)
--   * @servo@    — servo selection (speed, stall torque)
--   * @battery@  — battery selection (voltage, resistance, capacity)
--   * @dt@       — timestep [s]
--   * @bodyVel@  — body velocity [m/s]
--   * @bodyRates@— body angular rates [rad/s]
--
-- Returns @(VehicleOutput, next state)@. @VehicleOutput@ carries the total
-- force/moment, total mechanical power, and the peak separation fraction.
stepFirmwareVehicle
  :: RcChannels -> FirmwareParams -> ServoSpec -> BatterySpec
  -> Double -> Vec3 -> Vec3
  -> FirmwareVehicleState
  -> (VehicleOutput, FirmwareVehicleState)
stepFirmwareVehicle rc params servo battery dt bodyVel bodyRates state
  | dt <= 0 || dt > 0.05 = (zeroOutput 1, state)
  | otherwise =
      let -- 1. Firmware mixer: RC → wing servo commands (flap deviation deg).
          (mix, nextFw) = computeServoMixer rc params (fvFirmware state) dt

          -- 2. Servo struggle: track the commanded flap deviation against the
          --    hinge torque the wings produced LAST step (explicit one-step
          --    feedback delay — standard, stable game-loop coupling).
          voltage = batteryVoltageUnderLoad battery 0
          (leftFlapDeg, nextServoL) = stepServo servo battery
                                        (mixLeftFlapDevDeg mix)
                                        (fvLeftHingeTorqueNm state)
                                        voltage dt (fvServoLeft state)
          (rightFlapDeg, nextServoR) = stepServo servo battery
                                         (mixRightFlapDevDeg mix)
                                         (fvRightHingeTorqueNm state)
                                         voltage dt (fvServoRight state)

          -- 3. Actual flap deviation → wing physics.
          input = VehicleInput dt bodyVel bodyRates 0 0 0 0
          strokeL = radians leftFlapDeg
          strokeR = radians rightFlapDeg
          rateL = radians (servoRateDegPerSec nextServoL)
          rateR = radians (servoRateDegPerSec nextServoR)
          (leftResults, leftNext) = stepWingWithStroke (-1) strokeL rateL input (fvLeftStrips state)
          (rightResults, rightNext) = stepWingWithStroke 1 strokeR rateR input (fvRightStrips state)
          wingResults = leftResults ++ rightResults

          -- 4. Hinge torque = spanwise (y) aero moment the servo must overcome.
          leftHinge = sum (map (vy . resultMoment) leftResults)
          rightHinge = sum (map (vy . resultMoment) rightResults)

          -- 5. Assemble full-vehicle forces (wings + body drag + tail).
          --    Roll comes from the wing amplitude differential (already in the
          --    mixer); pitch/yaw use the tail surfaces, matching the firmware's
          --    elevator→pitch and rudder→yaw authority.
          elevatorNorm = crsfToNorm (rcElevator rc)
          rudderNorm = crsfToNorm (rcRudder rc)
          wingForce = foldr (addVec . resultForce) zeroVec wingResults
          wingMoment = foldr (addVec . resultMoment) zeroVec wingResults
          speed = magnitude bodyVel
          bodyDrag = scaleVec (-0.5 * 1.225 * 0.032 * speed) bodyVel
          tailArm = Vec3 (-0.48) 0 0
          tailVelocity = addVec bodyVel (crossVec bodyRates tailArm)
          tailQArea = 0.5 * 1.225 * 0.035 * magnitude tailVelocity * magnitude tailVelocity
          tailTrimDownforce = -0.18 * tailQArea
          tailForce = addVec (scaleVec tailQArea (Vec3 0 (-rudderNorm) (-elevatorNorm)))
                             (Vec3 0 0 tailTrimDownforce)
          tailMoment = crossVec tailArm tailForce
          force = addVec wingForce (addVec bodyDrag tailForce)
          moment = addVec wingMoment tailMoment

          separations = map (stripSeparation . resultState) wingResults
          maximumSeparation = maximum (0 : separations)
          power = sum (map resultPower wingResults)

          -- 6. Battery drain: servo current ∝ torque magnitude.
          socDrop = batterySocDrop servo battery (abs leftHinge + abs rightHinge) dt
          nextSoc = max 0 (fvBatterySoc state - socDrop)

          nextState = state
            { fvFirmware = nextFw
            , fvServoLeft = nextServoL
            , fvServoRight = nextServoR
            , fvLeftStrips = leftNext
            , fvRightStrips = rightNext
            , fvLeftHingeTorqueNm = leftHinge
            , fvRightHingeTorqueNm = rightHinge
            , fvBatterySoc = nextSoc
            }

          output = VehicleOutput force moment power maximumSeparation (-1) 0
      in (output, nextState)

-- | Battery state-of-charge drain per step, from servo torque. Current is
-- approximated as torque ∕ (voltage · torque-per-amp), capacity in Ah.
batterySocDrop :: ServoSpec -> BatterySpec -> Double -> Double -> Double
batterySocDrop servo battery torqueNm dt =
  let -- torque-per-amp: assume stall torque draws ~ 5 A at nominal voltage.
      torquePerAmp = servoStallTorqueNm servo / 5.0
      currentA = torqueNm / max 0.01 torquePerAmp
      ampHours = currentA * (dt / 3600)
  in ampHours / max 0.01 (batteryCapacityAh battery)

zeroOutput :: Int -> VehicleOutput
zeroOutput flags = VehicleOutput zeroVec zeroVec 0 0 (-1) flags