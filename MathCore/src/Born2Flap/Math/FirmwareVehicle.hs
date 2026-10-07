{-# LANGUAGE DerivingStrategies #-}
{-# LANGUAGE StrictData #-}

-- | The full "simulation in the simulation": firmware mixer → servo actuator
-- → wing aerodynamics → aerodynamic hinge torque feeding back into the servo.
--
-- The mixer derived from PteronautOS, with simulator glide/timing extensions,
-- reacts to RC pilot input; its wing-servo
-- commands are realised by a servo model that *struggles* against the real
-- aerodynamic hinge torque the wings produce; and the resulting actual flap
-- angle drives the wing physics. Servo, wing size, and battery are all
-- selectable. Coefficients and actuator parameters remain provisional and
-- require calibration against physical measurements.
module Born2Flap.Math.FirmwareVehicle
  ( FirmwareVehicleState(..)
  , defaultFirmwareVehicleState
  , stepFirmwareVehicle
  , hingeTorque
  ) where

import Born2Flap.Math.Types (Vec3(..))
import Born2Flap.Math.Firmware
import Born2Flap.Math.Servo
import Born2Flap.Math.Simulation
import Born2Flap.Math.Waveform (OscillatorState(..), limiarFromFerocities)
import Born2Flap.Math.PhaseEnvelope
import Born2Flap.Math.Resonance
import Born2Flap.Math.Vehicle
  ( VehicleInput(..), VehicleOutput(..)
  , StripState(..), StripResult(..), initialStrips
  , stepWingWithStroke, wingRootHeight
  , addVec, crossVec, scaleVec, magnitude, zeroVec, radians )

data FirmwareVehicleState = FirmwareVehicleState
  { fvFirmware        :: !FirmwareState
  , fvServoLeft       :: !ServoState
  , fvServoRight      :: !ServoState
  , fvLeftStrips      :: ![StripState]
  , fvRightStrips     :: ![StripState]
  , fvLeftHingeTorqueNm  :: !Double   -- ^ aero load fed to the servo next step
  , fvRightHingeTorqueNm :: !Double
  , fvBatterySoc      :: !Double     -- ^ 0..1, drains under servo current
  , fvPhaseEnvelope   :: !PhaseEnvelopeState  -- ^ ONDAS A-layer
  , fvResonance       :: !ResonanceState      -- ^ ONDAS C-layer
  , fvStabilized      :: !Bool      -- ^ ONDAS stabilized-mode master switch
  , fvWindPhaseNoise  :: !Double    -- ^ wind phase noise η [rad/s] (environmental)
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
  , fvPhaseEnvelope = defaultPhaseEnvelope
  , fvResonance = defaultResonance
  , fvStabilized = False
  , fvWindPhaseNoise = 0
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
  :: Bool -> RcChannels -> FirmwareParams -> ServoSpec -> BatterySpec -> Double
  -> Double -> Vec3 -> Vec3
  -> FirmwareVehicleState
  -> (VehicleOutput, FirmwareVehicleState)
stepFirmwareVehicle coupled rc params servo battery wingScale dt bodyVel bodyRates state
  | not (finite dt) || dt <= 0 || dt > 0.05 = (zeroOutput 1, state)
  | not (all finite (components bodyVel ++ components bodyRates ++
      [rcAileron rc, rcElevator rc, rcThrottle rc, rcRudder rc, rcArm rc, rcFreq rc, rcProfile rc])) =
      (zeroOutput 2, state)
  | otherwise = case runSimulation transaction state of
      Left flag -> (zeroOutput flag, state)
      Right result -> result
  where
    transaction = do
      old <- getState
      let (output, next) = advanceFirmwareVehicle coupled rc params servo battery wingScale dt bodyVel bodyRates old
          values = components (totalForceN output) ++ components (totalMomentNm output) ++
            [totalMechanicalPowerW output, maxSeparation output, fvBatterySoc next,
             fvLeftHingeTorqueNm next, fvRightHingeTorqueNm next,
             servoAngleDeg (fvServoLeft next), servoAngleDeg (fvServoRight next),
             servoRateDegPerSec (fvServoLeft next), servoRateDegPerSec (fvServoRight next),
             phaseEnvelope (fvPhaseEnvelope next), phaseCoverage (fvPhaseEnvelope next)]
      if all finite values then putState next >> pure output else abort 2

finite :: Double -> Bool
finite value = not (isNaN value || isInfinite value)

components :: Vec3 -> [Double]
components (Vec3 a b c) = [a,b,c]

-- Both positive stroke coordinates raise the wing. The left hinge axis is -X.
hingeTorque :: Double -> [StripResult] -> Double
hingeTorque side = (* side) . sum . map (\r -> x (resultMoment r) + wingRootHeight * y (resultForce r))

advanceFirmwareVehicle
  :: Bool -> RcChannels -> FirmwareParams -> ServoSpec -> BatterySpec -> Double
  -> Double -> Vec3 -> Vec3 -> FirmwareVehicleState -> (VehicleOutput, FirmwareVehicleState)
advanceFirmwareVehicle coupled rc params servo battery wingScale dt bodyVel bodyRates state
  | otherwise =
      let -- 1. Firmware mixer: RC → wing servo commands (flap deviation deg).
          (mix, nextFw) = computeServoMixer coupled rc params (fvFirmware state) dt

          -- 2. Servo struggle: track the commanded flap deviation against the
          --    hinge torque the wings produced LAST step (explicit one-step
          --    feedback delay — standard, stable game-loop coupling).
          currentA = min 10 ((abs (fvLeftHingeTorqueNm state) + abs (fvRightHingeTorqueNm state))
                       / max 0.01 (servoStallTorqueNm servo / 5))
          liveBattery = battery { batteryStateOfCharge = fvBatterySoc state }
          voltage = batteryVoltageUnderLoad liveBattery currentA
          (leftFlapDeg, nextServoL) = stepServo servo battery
                                        (mixLeftFlapDevDeg mix)
                                        (fvLeftHingeTorqueNm state)
                                        wingScale voltage dt (fvServoLeft state)
          (rightFlapDeg, nextServoR) = stepServo servo battery
                                         (mixRightFlapDevDeg mix)
                                         (fvRightHingeTorqueNm state)
                                         wingScale voltage dt (fvServoRight state)

          -- 2b. ONDAS A-injection: golden-angle phase-envelope strobe at reversal.
          prevPhase = oscPhase (fwOscillator (fvFirmware state))
          limiar = limiarFromFerocities (mixStrokeFerL mix) (mixReturnFerL mix)
          reversal = mixIsFlapping mix
                     && detectReversal prevPhase (mixPhase mix) limiar
          -- Signed servo tracking error: the Vold–Kalman tracker extracts its
          -- phase-coherent ω-component, so the RESONANCE layer must see the
          -- SIGNED lag (an |abs| would double the frequency and read ~0 at ω).
          signedTrackErr = mixLeftFlapDevDeg mix - servoAngleDeg nextServoL
          trackErr = abs signedTrackErr
          peNext = if reversal then phaseStrobe trackErr (fvPhaseEnvelope state)
                                else fvPhaseEnvelope state

          -- 2c. ONDAS C-injection: Vold–Kalman engagement → phase-lock demand.
          omega = oscCadence (fwOscillator nextFw)
          (kGainModDemand, nextResonance) =
            stepResonance signedTrackErr omega (fvWindPhaseNoise state) dt (fvResonance state)
          -- The stabilized mode closes the A-layer: the golden-angle strobe's
          -- coverage (0..1) gates the resonance demand, so the phase-lock only
          -- acts once the cycle has been read honestly — never on an aliased
          -- overtone. Off-mode holds the oscillator at nominal phase-advance 1.
          aGate = if fvStabilized state
                  then max 0 (min 1 ((phaseCoverage peNext - 0.5) / 0.5))
                  else 0
          kGainModNext = 1 + aGate * (kGainModDemand - 1)
          nextFw' = nextFw { fwKGainMod = kGainModNext }

          -- 3. Actual flap deviation → wing physics.
          input = VehicleInput dt bodyVel bodyRates 0 (crsfToNorm (rcAileron rc)) elevatorNorm 0
          wingIncidenceRad = radians (fwMountIncidenceDeg params)
          strokeL = radians leftFlapDeg
          strokeR = radians rightFlapDeg
          rateL = radians (servoRateDegPerSec nextServoL)
          rateR = radians (servoRateDegPerSec nextServoR)
          (leftResults, leftNext) = stepWingWithStroke (mixIsFlapping mix) (-1) strokeL rateL input wingScale wingIncidenceRad (fvLeftStrips state)
          (rightResults, rightNext) = stepWingWithStroke (mixIsFlapping mix) 1 strokeR rateR input wingScale wingIncidenceRad (fvRightStrips state)
          wingResults = leftResults ++ rightResults

          -- 4. Generalized torque conjugate to each wing's flap coordinate.
          leftHinge = hingeTorque (-1) leftResults
          rightHinge = hingeTorque 1 rightResults

          -- 5. Assemble full-vehicle forces (wings + body drag + tail).
          --    Every control moment is a force acting at a physical lever arm.
          --    Tail flow includes body angular velocity, providing aerodynamic
          --    pitch/yaw damping instead of an artificial attitude torque.
          elevatorNorm = crsfToNorm (rcElevator rc)
          rudderNorm = (mixRudderUs mix - 1500) / 500
          wingForce = foldr (addVec . resultForce) zeroVec wingResults
          wingMoment = foldr (addVec . resultMoment) zeroVec wingResults
          speed = magnitude bodyVel
          -- Frontal area scales with the planform scale squared (geometric
          -- similarity): a small bird must not carry a full-size body's drag.
          bodyDrag = scaleVec (-0.5 * 1.225 * 0.018 * wingScale * wingScale * speed) bodyVel
          -- Two real inverted-V panels, 12 degrees down, with mixed ruddervators.
          -- Each panel sees its own local flow and produces force at its own arm;
          -- the resulting pitch/yaw/roll coupling is geometric, not an added torque.
          cant = radians 12
          -- A direct flap-power/ruddervator mix balances the wing drive's changing
          -- pitching moment. Glide returns to its shallow incidence; no attitude,
          -- altitude or speed target is involved.
          tailTrim = -2.0 - 4.0 * mixThrottlePct mix * mixThrottlePct mix
          tailPanel side =
            let -- Tail geometry scales with the bird: arm ∝ wingScale,
                -- panel area ∝ wingScale² (else a micro bird carries a
                -- full-size tail whose downforce overwhelms its weight).
                arm = Vec3 (-0.85 * wingScale) (side * 0.12 * wingScale) (-0.12 * wingScale * tan cant)
                normal = Vec3 0 (side * sin cant) (cos cant)
                flow = addVec bodyVel (crossVec bodyRates arm)
                normalFlow = y flow * y normal + z flow * z normal
                -- Ruddervator deflection is a rotation about the spanwise hinge,
                -- i.e. a *direct* change of panel incidence (no sin/cos here).
                -- The projection onto body yaw/pitch axes is already done by the
                -- panel normal below; folding sin(cant)/cos(cant) into the
                -- incidence double-projects and throttles the rudder by sin²(cant).
                incidence = radians (tailTrim - 18 * elevatorNorm
                                      - side * 30 * rudderNorm)
                (fx, fn) = tailSurface (0.0242 * wingScale * wingScale) incidence (x flow) normalFlow
                panelForce = addVec (Vec3 fx 0 0) (scaleVec fn normal)
            in (panelForce, crossVec arm panelForce)
          panels = map tailPanel [-1,1]
          tailForce = foldr (addVec . fst) zeroVec panels
          tailMoment = foldr (addVec . snd) zeroVec panels
          force = addVec wingForce (addVec bodyDrag tailForce)
          moment = addVec wingMoment tailMoment

          separations = map (stripSeparation . resultState) wingResults
          maximumSeparation = maximum (0 : separations)
          power = sum (map resultPower wingResults)

          -- 6. Battery drain: servo current ∝ torque magnitude.
          socDrop = batterySocDrop servo battery (abs leftHinge + abs rightHinge) dt
          nextSoc = max 0 (fvBatterySoc state - socDrop)

          nextState = state
            { fvFirmware = nextFw'
            , fvServoLeft = nextServoL
            , fvServoRight = nextServoR
            , fvLeftStrips = leftNext
            , fvRightStrips = rightNext
            , fvLeftHingeTorqueNm = leftHinge
            , fvRightHingeTorqueNm = rightHinge
            , fvBatterySoc = nextSoc
            , fvPhaseEnvelope = peNext
            , fvResonance = nextResonance
            }

          output = VehicleOutput force moment power maximumSeparation (-1) 0
      in (output, nextState)

-- | Battery state-of-charge drain per step, from servo torque. Current is
-- approximated as torque ∕ (voltage · torque-per-amp), capacity in Ah.
batterySocDrop :: ServoSpec -> BatterySpec -> Double -> Double -> Double
batterySocDrop servo battery torqueNm dt =
  let -- torque-per-amp: assume stall torque draws ~ 5 A at nominal voltage.
      torquePerAmp = servoStallTorqueNm servo / 5.0
      currentA = min 10 (torqueNm / max 0.01 torquePerAmp)
      ampHours = currentA * (dt / 3600)
  in ampHours / max 0.01 (batteryCapacityAh battery)

zeroOutput :: Int -> VehicleOutput
zeroOutput flags = VehicleOutput zeroVec zeroVec 0 0 (-1) flags

-- Provisional low-aspect-ratio tail polar. Even a deflected surface cannot
-- create translational energy: lift is perpendicular, Cd is nonnegative.
tailSurface :: Double -> Double -> Double -> Double -> (Double, Double)
tailSurface area incidence chordVelocity normalVelocity =
  let speed = sqrt (chordVelocity * chordVelocity + normalVelocity * normalVelocity)
      alpha = incidence + atan2 (-normalVelocity) chordVelocity
      cl = 1.1 * sin (2 * alpha)
      cd = 0.025 + 0.14 * cl * cl + 0.8 * sin alpha * sin alpha
      q = 0.5 * 1.225 * area * speed * speed
      u = chordVelocity / max 1.0e-9 speed
      w = normalVelocity / max 1.0e-9 speed
  in (q * (-cd * u - cl * w), q * (-cd * w + cl * u))