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
  , batteryVoltageUnderLoad
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
  { servoNoLoadSpeedDegPerSec = 60 / (70 * 0.001)  -- 70 ms/60° → 857 °/s
  , servoStallTorqueNm = 2.0
  , servoBackdriveDegPerSecNm = 20
  }

-- ── Battery specification ──────────────────────────────────────────

data BatterySpec = BatterySpec
  { batteryNominalVoltage :: !Double  -- ^ V (e.g. 7.4 = 2S LiPo)
  , batteryInternalResistanceOhm :: !Double
  , batteryCapacityAh :: !Double
  , batteryStateOfCharge :: !Double  -- ^ 0..1
  } deriving stock (Eq, Show)

defaultBatterySpec :: BatterySpec
defaultBatterySpec = BatterySpec
  { batteryNominalVoltage = 7.4
  , batteryInternalResistanceOhm = 0.05
  , batteryCapacityAh = 0.35
  , batteryStateOfCharge = 1.0
  }

-- | Terminal voltage under a current draw: V = V_nom·soc − I·R_internal.
batteryVoltageUnderLoad :: BatterySpec -> Double -> Double
batteryVoltageUnderLoad battery currentDrawA =
  max 0 (batteryNominalVoltage battery * batteryStateOfCharge battery
         - currentDrawA * batteryInternalResistanceOhm battery)

-- ── Servo state ────────────────────────────────────────────────────

data ServoState = ServoState
  { servoAngleDeg :: !Double   -- ^ actual output-shaft angle
  , servoRateDegPerSec :: !Double  -- ^ actual slew rate
  } deriving stock (Eq, Show)

defaultServoState :: ServoState
defaultServoState = ServoState 0 0

-- | Advance one servo step.
--
--   * @targetDeg@   — commanded flap-deviation angle (from the firmware mixer)
--   * @loadTorqueNm@ — aerodynamic hinge torque the wing exerts on the servo
--                     shaft, signed by the torque the load applies: positive
--                     tries to rotate the shaft in the positive-angle direction.
--   * @voltage@     — battery terminal voltage (drives the voltage factor)
--   * @dt@          — timestep
--
-- Torque–speed curve: available torque falls linearly from stall torque at
-- zero speed to zero at the no-load speed, scaled by @voltage / V_nom@.
-- When the load exceeds the available torque the servo is back-driven by the
-- excess (the wing wins), otherwise it tracks the target at the load-reduced
-- speed. This is the physical "struggle".
stepServo :: ServoSpec -> BatterySpec -> Double -> Double -> Double -> Double
          -> ServoState -> (Double, ServoState)
stepServo servo battery targetDeg loadTorqueNm voltage dt state
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
          smoothRate = servoRateDegPerSec state + (requestedRate - servoRateDegPerSec state) * (1 - exp (-dt / 0.025))
          limitedRate = clampRate (negate (servoNoLoadSpeedDegPerSec servo))
                                  (servoNoLoadSpeedDegPerSec servo) smoothRate
          angle = clampRate (-80) 80 (servoAngleDeg state + limitedRate * dt)
          next = ServoState angle ((angle - servoAngleDeg state) / dt)
      in (angle, next)

clampRate :: Double -> Double -> Double -> Double
clampRate low high = max low . min high
