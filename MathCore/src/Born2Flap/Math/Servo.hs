{-# LANGUAGE DerivingStrategies #-}
{-# LANGUAGE StrictData #-}

-- | Servo actuator + battery model: the physical layer between the firmware
-- mixer's angle commands and the wing aerodynamics. A servo is a closed-loop
-- position tracker limited by two physical constraints:
--
--   1. *Speed* — back-EMF limits the no-load slew rate (deg/s).
--   2. *Torque* — the stall torque; aerodynamic hinge load consumes torque,
--      so under load the servo slows, and beyond stall torque it is
--      back-driven (the wing pushes the servo instead of the reverse).
--
-- Battery voltage sag under load scales BOTH limits through a voltage
-- factor, so a sagging battery makes the bird flap slower and weaker — the
-- "servo struggling" behaviour the owner wants to feel.
module Born2Flap.Math.Servo
  ( ServoSpec(..)
  , defaultServoSpec
  , BatterySpec(..)
  , defaultBatterySpec
  , ServoState(..)
  , defaultServoState
  , stepServo
  , servoDriveTorque
  , servoBackdriveTorque
  , reflectedDriveInertia
  , batteryVoltageUnderLoad
  , batteryVoltageUnderLoadBoost
  ) where

-- ── Servo specification (selectable in the game) ───────────────────

-- The servo is modelled in *flap-deviation* space: 0° = level wing, and the
-- linkage/gearing that maps the firmware's raw 100° shaft neutral to 0° flap
-- deviation is absorbed here (the firmware's @ORNI_ANGULAR_MULTIPLIER@).
data ServoSpec = ServoSpec
  { servoNoLoadSpeedDegPerSec :: !Double  -- ^ no-load slew rate (e.g. 857)
  , servoStallTorqueNm        :: !Double  -- ^ stall torque (e.g. 2.0)
  , servoBackdriveDegPerSecNm :: !Double  -- ^ compliance when overpowered
  } deriving stock (Eq, Show)

defaultServoSpec :: ServoSpec
defaultServoSpec = ServoSpec
  { servoNoLoadSpeedDegPerSec = 1500  -- strong HV servo (40 ms/60°)
  , servoStallTorqueNm = 1.332       -- strong metal-gear stall torque
  , servoBackdriveDegPerSecNm = 20
  }

-- ── Battery specification ──────────────────────────────────────────

data BatterySpec = BatterySpec
  { batteryNominalVoltage :: !Double  -- ^ V (e.g. 7.4 = 2S LiPo)
  , batteryInternalResistanceOhm :: !Double
  , batteryCapacityAh :: !Double
  , batteryStateOfCharge :: !Double  -- ^ 0..1
  , batteryCRating :: !Double        -- ^ continuous discharge rating (C), e.g. 30
  , batteryBurstCRating :: !Double   -- ^ burst discharge rating (C), e.g. 60 = momentarily double
  } deriving stock (Eq, Show)

defaultBatterySpec :: BatterySpec
defaultBatterySpec = BatterySpec
  { batteryNominalVoltage = 7.4
  , batteryInternalResistanceOhm = 0.05
  , batteryCapacityAh = 0.35
  , batteryStateOfCharge = 1.0
  , batteryCRating = 30
  , batteryBurstCRating = 60
  }

-- | Terminal voltage under a current draw: V = V_nom·soc − I·R_internal.
batteryVoltageUnderLoad :: BatterySpec -> Double -> Double
batteryVoltageUnderLoad battery currentDrawA =
  max 0 (batteryNominalVoltage battery * batteryStateOfCharge battery
         - currentDrawA * batteryInternalResistanceOhm battery)

-- | Terminal voltage under load with the LiPo burst reservoir. A C-rated pack
-- can only sustain its continuous current (C·Ah); the burst rating (e.g. 60C vs
-- 30C) lets it momentarily deliver up to double that — while the burst
-- reservoir @boost@ (0..1) holds energy, the effective internal resistance
-- drops, so a hard downstroke punch sags less and the servo punches harder. As
-- the reservoir drains the sag returns. This is the battery-side half of the
-- ferocity cost: a snapped stroke needs the burst, and pays for it in sag.
batteryVoltageUnderLoadBoost :: BatterySpec -> Double -> Double -> Double
batteryVoltageUnderLoadBoost battery currentDrawA boost =
  let contA = max 0.01 (batteryCRating battery * batteryCapacityAh battery)
      burstA = max contA (batteryBurstCRating battery * batteryCapacityAh battery)
      -- Sag halves at full boost for a 2× burst rating, linearly interpolated.
      sagFactor = 1 / (1 + (burstA / contA - 1) * clampRate 0 1 boost)
      rEff = batteryInternalResistanceOhm battery * sagFactor
  in max 0 (batteryNominalVoltage battery * batteryStateOfCharge battery
            - currentDrawA * rEff)

-- ── Servo state ────────────────────────────────────────────────────

data ServoState = ServoState
  { servoAngleDeg :: !Double   -- ^ actual output-shaft angle
  , servoRateDegPerSec :: !Double  -- ^ actual slew rate
  } deriving stock (Eq, Show)

defaultServoState :: ServoState
defaultServoState = ServoState 0 0

-- Reflected rotor/gearing inertia is relative to the case, not additional
-- wing mass. It belongs on the joint diagonal, not in body recoil.
reflectedDriveInertia :: Double
reflectedDriveInertia = 0.0016

-- | Convert the existing position/speed controller's demand into bounded
-- motor torque. The fixed-base calculation is only a controller reference;
-- actual motion is integrated by the coupled body/joint mass matrix.
-- This preserves the established servo response without prescribing actual
-- joint acceleration independently of the moving fuselage.
servoDriveTorque :: ServoSpec -> BatterySpec -> Double -> Double -> Double -> Double -> Double -> ServoState -> Double -> Double
servoDriveTorque servo battery voltage dt target load wingInertia old rate =
  let vf = clampRate 0 1 (voltage / max 0.01 (batteryNominalVoltage battery))
      stall = max 0 (servoStallTorqueNm servo * vf)
      (_, reference) = stepServo servo battery target load wingInertia voltage dt old
      acceleration = ((2*servoRateDegPerSec reference-servoRateDegPerSec old)*pi/180-rate)/dt
      demand = (wingInertia+reflectedDriveInertia)*acceleration-load
  in clampRate (-stall) stall demand

-- | Passive gearbox resistance when an external load overpowers the drive.
-- Retains the user's backdrive compliance setting without prescribing joint
-- motion. This torque always dissipates energy and cannot power a dead servo.
servoBackdriveTorque :: ServoSpec -> BatterySpec -> Double -> Double -> Double -> Double
servoBackdriveTorque servo battery voltage load rate =
  let vf = clampRate 0 1 (voltage / max 0.01 (batteryNominalVoltage battery))
      stall = max 0 (servoStallTorqueNm servo * vf)
      engaged = clampRate 0 1 ((abs load-stall) / max 0.05 (0.2*stall))
      compliance = max 0.001 (servoBackdriveDegPerSecNm servo) * pi/180
  in -engaged * signum load * max 0 (rate*signum load) / compliance

-- | Advance one servo step.
--
--   * @targetDeg@   — commanded flap-deviation angle (from the firmware mixer)
--   * @loadTorqueNm@ — aerodynamic hinge torque the wing exerts on the servo
--                     shaft, signed by the torque the load applies: positive
--                     tries to rotate the shaft in the positive-angle direction.
--   * @wingInertia@ — wing moment of inertia about the flap hinge [kg·m²],
--                     derived from the planform (structural + added mass).
--   * @voltage@     — battery terminal voltage (drives the voltage factor)
--   * @dt@          — timestep
--
-- Torque–speed curve: available torque falls linearly from stall torque at
-- zero speed to zero at the no-load speed, scaled by @voltage / V_nom@.
-- When the load exceeds the available torque the servo is back-driven by the
-- excess (the wing wins), otherwise it tracks the target at the load-reduced
-- speed. This is the physical "struggle".
stepServo :: ServoSpec -> BatterySpec -> Double -> Double -> Double -> Double -> Double
          -> ServoState -> (Double, ServoState)
stepServo servo battery targetDeg loadTorqueNm wingInertia voltage dt state
  | dt <= 0 = (servoAngleDeg state, state)
  | otherwise =
      let voltFactor = clampRate 0 1 (voltage / max 0.01 (batteryNominalVoltage battery))
          stallT = max 1.0e-6 (servoStallTorqueNm servo * voltFactor)
          maxRate = servoNoLoadSpeedDegPerSec servo * voltFactor
          err = clampRate (-80) 80 targetDeg - servoAngleDeg state
          loadMag = abs loadTorqueNm
          available = stallT - loadMag
          requestedRate = if available <= 0
           then -- Overpowered: the aerodynamic load back-drives the servo in
                -- its own torque direction (the wing wins).
                let excess = loadMag - stallT
                    -- Passive backdrive remains possible with the motor unpowered.
                    backRate = min (servoNoLoadSpeedDegPerSec servo)
                                   (servoBackdriveDegPerSecNm servo * excess)
                in signum loadTorqueNm * backRate
           else -- Tracking, speed reduced by the load fraction.
                let opposingLoad = max 0 (negate (signum err * loadTorqueNm))
                    speed = maxRate * (1 - opposingLoad / stallT)
                    wantRate = err / dt
                    rate = clampRate (-speed) speed wantRate
                in rate
          -- Finite actuator response plus linkage end stops. Backdrive must
          -- never integrate an unlimited angle/speed into the aero loop.
          -- The servo accelerates its *rate* toward the requested rate, but
          -- that acceleration is limited by the torque still available after
          -- the aerodynamic hinge load is overcome: α_max = (stallT − load)/I.
          -- @wingInertia@ is the DERIVED moment of inertia of one wing about
          -- the flap hinge (structural mass + entrained-air added mass), computed
          -- from the planform by @shapeFlapInertia@ and passed in by the caller.
          -- A heavier wing does NOT make the servo respond more slowly in time
          -- — it demands MORE TORQUE to reach the same angular acceleration, so
          -- servo power genuinely shows up in how quickly the stroke reverses.
          servoInertia = reflectedDriveInertia
          inertia = servoInertia + wingInertia
          -- The torque available to accelerate the rate. When tracking, the
          -- servo's spare torque (stallT − load) drives it; when overpowered,
          -- the aerodynamic load's EXCESS torque (load − stallT) is what back-
          -- drives the wing, so |stallT − load| is the right driving torque in
          -- both regimes. Without the absolute value a back-driven servo would
          -- be clamped to zero acceleration and never actually move.
          accelTorque = abs (stallT - abs loadTorqueNm)
          alphaMaxDeg = (accelTorque / inertia) * (180 / pi)   -- deg/s²
          rateStep = clampRate (negate (alphaMaxDeg * dt)) (alphaMaxDeg * dt)
                               (requestedRate - servoRateDegPerSec state)
          smoothRate = servoRateDegPerSec state + rateStep
          limitedRate = clampRate (negate (servoNoLoadSpeedDegPerSec servo))
                                  (servoNoLoadSpeedDegPerSec servo) smoothRate
          angle = clampRate (-80) 80 (servoAngleDeg state + limitedRate * dt)
          next = ServoState angle ((angle - servoAngleDeg state) / dt)
      in (angle, next)

clampRate :: Double -> Double -> Double -> Double
clampRate low high = max low . min high