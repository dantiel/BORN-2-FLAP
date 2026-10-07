{-# LANGUAGE ForeignFunctionInterface #-}

module Born2Flap.Math.FFI where

import Born2Flap.Math.Types (Vec3(..))
import Born2Flap.Math.Vehicle
import Born2Flap.Math.Planform (defaultBirdWing, shapeChord, shapeTwistRad)
import Born2Flap.Math.PhaseEnvelope (phaseEnvelope, phaseCoverage)
import Born2Flap.Math.Resonance (rsPhaseError)
import Born2Flap.Math.Firmware
  ( FirmwareParams(..), FlightProfile(..), defaultFirmwareParams, FirmwareState(..)
  , PilotInput(..), defaultPilotInput, pilotToRc )
import Born2Flap.Math.FirmwareVehicle
import Born2Flap.Math.Servo
  ( ServoSpec(..), BatterySpec(..), ServoState(..)
  , defaultServoSpec, defaultBatterySpec )
import Control.Exception (SomeException, catch)
import Data.IORef
import Data.Int (Int32)
import Data.Word (Word32)
import Foreign.C.Types (CDouble(..), CInt(..), CUInt(..))
import Foreign.Ptr
import Foreign.StablePtr
import Foreign.Storable

foreign export ccall "hs_b2f_math_abi_version" b2f_math_abi_version :: IO Word32
foreign export ccall "hs_b2f_math_runtime_init" b2f_math_runtime_init :: IO Int32
foreign export ccall "hs_b2f_math_runtime_shutdown" b2f_math_runtime_shutdown :: IO ()
foreign export ccall "hs_b2f_math_create_default_vehicle" b2f_math_create_default_vehicle :: IO (Ptr ())
foreign export ccall "hs_b2f_math_destroy_vehicle" b2f_math_destroy_vehicle :: Ptr () -> IO ()
foreign export ccall "hs_b2f_math_step_vehicle" b2f_math_step_vehicle :: Ptr () -> Ptr () -> Ptr () -> IO Int32
foreign export ccall "hs_b2f_math_create_firmware_vehicle" b2f_math_create_firmware_vehicle :: Ptr () -> IO (Ptr ())
foreign export ccall "hs_b2f_math_destroy_firmware_vehicle" b2f_math_destroy_firmware_vehicle :: Ptr () -> IO ()
foreign export ccall "hs_b2f_math_step_firmware_vehicle" b2f_math_step_firmware_vehicle :: Ptr () -> Ptr () -> Ptr () -> Ptr () -> IO Int32
foreign export ccall "hs_b2f_math_set_stabilization" b2f_math_set_stabilization :: Ptr () -> Word32 -> IO Int32
foreign export ccall "hs_b2f_math_set_wind_phase_noise" b2f_math_set_wind_phase_noise :: Ptr () -> Double -> IO Int32
foreign export ccall "hs_b2f_math_reconfigure_firmware_vehicle" b2f_math_reconfigure_firmware_vehicle :: Ptr () -> Ptr () -> IO Int32
foreign export ccall "hs_b2f_math_get_wing_shape" b2f_math_get_wing_shape :: Ptr () -> Word32 -> Ptr () -> Ptr () -> IO Int32
b2f_math_get_wing_shape :: Ptr () -> Word32 -> Ptr () -> Ptr () -> IO Int32
b2f_math_get_wing_shape contextPointer capacity leftPointer rightPointer
  | contextPointer == nullPtr || leftPointer == nullPtr || rightPointer == nullPtr
      || capacity < fromIntegral stripCount = pure 0
  | otherwise = run `catch` failure
  where
    run = do
      contextRef <- deRefStablePtr (castPtrToStablePtr contextPointer :: StablePtr (IORef FwContext))
      context <- readIORef contextRef
      let state = fwcState context
          values strips = concat
            [ let fraction = (fromIntegral i + 0.5) / fromIntegral stripCount
              in [fraction, shapeChord defaultBirdWing fraction, stripBendM s,
                  shapeTwistRad defaultBirdWing fraction + stripTwistAero s, stripCamber s]
            | (i, s) <- zip [0 :: Int ..] strips ]
          left = values (fvLeftStrips state)
          right = values (fvRightStrips state)
          valid xs = length xs == stripCount * 5 && all (\v -> not (isNaN v || isInfinite v)) xs
          write pointer xs = sequence_
            [pokeElemOff (castPtr pointer :: Ptr CDouble) i (CDouble v) | (i,v) <- zip [0..] xs]
      if not (valid left && valid right) then pure 0 else do
        write leftPointer left
        write rightPointer right
        pure (fromIntegral stripCount)
    failure :: SomeException -> IO Int32
    failure _ = pure 0
b2f_math_abi_version :: IO Word32
b2f_math_abi_version = pure 8

b2f_math_runtime_init :: IO Int32
b2f_math_runtime_init = pure 1

b2f_math_runtime_shutdown :: IO ()
b2f_math_runtime_shutdown = pure ()

b2f_math_create_default_vehicle :: IO (Ptr ())
b2f_math_create_default_vehicle =
  castStablePtrToPtr <$> (newIORef defaultVehicle >>= newStablePtr)

b2f_math_destroy_vehicle :: Ptr () -> IO ()
b2f_math_destroy_vehicle pointer
  | pointer == nullPtr = pure ()
  | otherwise = freeStablePtr (castPtrToStablePtr pointer :: StablePtr (IORef VehicleState))

b2f_math_step_vehicle :: Ptr () -> Ptr () -> Ptr () -> IO Int32
b2f_math_step_vehicle contextPointer inputPointer outputPointer
  | contextPointer == nullPtr || inputPointer == nullPtr || outputPointer == nullPtr = pure 0
  | otherwise = run `catch` failure
  where
    run = do
      stateRef <- deRefStablePtr (castPtrToStablePtr contextPointer :: StablePtr (IORef VehicleState))
      input <- peekInput inputPointer
      state <- readIORef stateRef
      let (output, nextState) = stepVehicle input state
      writeIORef stateRef nextState
      pokeOutput outputPointer output
      pure (if outputFlags output == 0 then 1 else 0)
    failure :: SomeException -> IO Int32
    failure _ = pure 0

peekInput :: Ptr () -> IO VehicleInput
peekInput pointer = do
  values <- mapM (peekElemOff (castPtr pointer :: Ptr CDouble)) [0 .. 10]
  let doubles = map (\(CDouble value) -> value) values
  case doubles of
    [dt, lvx, lvy, lvz, avx, avy, avz, throttle, roll, pitch, yaw] ->
      pure VehicleInput
        { stepSeconds = dt
        , bodyVelocityMS = Vec3 lvx lvy lvz
        , bodyRatesRadS = Vec3 avx avy avz
        , throttleCommand = throttle
        , rollCommand = roll
        , pitchCommand = pitch
        , yawCommand = yaw
        }
    _ -> error "unreachable input layout"

pokeOutput :: Ptr () -> VehicleOutput -> IO ()
pokeOutput pointer output = do
  let Vec3 fx fy fz = totalForceN output
      Vec3 mx my mz = totalMomentNm output
      doubles = [fx, fy, fz, mx, my, mz, totalMechanicalPowerW output, maxSeparation output]
      doublePointer = castPtr pointer :: Ptr CDouble
  sequence_ [pokeElemOff doublePointer index (CDouble value) | (index, value) <- zip [0 ..] doubles]
  pokeByteOff pointer 64 (CInt (fromIntegral (firstStalledElement output)))
  pokeByteOff pointer 68 (CUInt (fromIntegral (outputFlags output)))

-- ── Firmware vehicle ABI (logical channels, selectable components) ─

-- | Mutable firmware-vehicle context: fixed component selection (servo +
-- battery + mixer params) around a mutable loop state.
data FwContext = FwContext
  { fwcParams     :: !FirmwareParams
  , fwcServo      :: !ServoSpec
  , fwcBattery    :: !BatterySpec
  , fwcState      :: !FirmwareVehicleState
  , fwcWingScale  :: !Double  -- ^ planform scale (sized to body mass)
  }

-- | Raw component-selection config (6 doubles, 0 = use default).
data FwConfig = FwConfig
  { cfgServoSpeed        :: !Double
  , cfgStallTorque       :: !Double
  , cfgBackdrive         :: !Double
  , cfgBatteryVoltage    :: !Double
  , cfgBatteryResistance :: !Double
  , cfgBatteryCapacity   :: !Double
  }

-- | Live tuning profile: servo + battery + controller knobs the in-game hangar
-- edits. Every field is an explicit double — the C++ host always sends a
-- complete profile (no \"0 = default\" sentinel).
data TuningConfig = TuningConfig
  { tgServoSpeed        :: !Double  -- ^ no-load slew rate [deg/s]
  , tgStallTorque       :: !Double  -- ^ stall torque [N·m]
  , tgBackdrive         :: !Double  -- ^ backdrive compliance [deg/s per N·m]
  , tgBatteryVoltage    :: !Double  -- ^ nominal voltage [V]
  , tgBatteryResistance :: !Double  -- ^ internal resistance [Ω]
  , tgBatteryCapacity   :: !Double  -- ^ capacity [Ah]
  , tgFlapBaseFreqDh    :: !Double  -- ^ flap frequency ceiling [deci-Hz, 10..200]
  , tgTailElevatorAngleDeg :: !Double  -- ^ tail elevator angle (flap stroke centre trim) [deg]
  , tgGlideAngleDeg     :: !Double  -- ^ glide incidence [deg]
  , tgStrokeFerocity    :: !Double  -- ^ downstroke ferocity [0..100]
  , tgAileronScale      :: !Double  -- ^ aileron mix [0..100]
  , tgElevatorScale     :: !Double  -- ^ elevator mix [0..100]
  , tgMountAngleDeg     :: !Double  -- ^ wing mount incidence (common flap/glide trim) [deg]
  , tgBodyMassKg        :: !Double  -- ^ airframe mass [kg], scales the wing planform
  }

b2f_math_create_firmware_vehicle :: Ptr () -> IO (Ptr ())
b2f_math_create_firmware_vehicle configPointer = do
  config <- if configPointer == nullPtr
              then pure (FwConfig 0 0 0 0 0 0)
              else peekConfig configPointer
  let servo = defaultServoSpec
        { servoNoLoadSpeedDegPerSec = posOr (servoNoLoadSpeedDegPerSec defaultServoSpec) (cfgServoSpeed config)
        , servoStallTorqueNm = posOr (servoStallTorqueNm defaultServoSpec) (cfgStallTorque config)
        , servoBackdriveDegPerSecNm = posOr (servoBackdriveDegPerSecNm defaultServoSpec) (cfgBackdrive config)
        }
      battery = defaultBatterySpec
        { batteryNominalVoltage = posOr (batteryNominalVoltage defaultBatterySpec) (cfgBatteryVoltage config)
        , batteryInternalResistanceOhm = posOr (batteryInternalResistanceOhm defaultBatterySpec) (cfgBatteryResistance config)
        , batteryCapacityAh = posOr (batteryCapacityAh defaultBatterySpec) (cfgBatteryCapacity config)
        }
      -- Match cadence/amplitude demand to the selected actuator and select
      -- the simulator's RC amplitude/timing/position mixes for this vehicle.
      -- A full stroke at excessive cadence used to saturate the actuator, so
      -- more throttle barely changed its actual motion.
      flightParams = defaultFirmwareParams
        { fwServoSpeedMs = 60000 / servoNoLoadSpeedDegPerSec servo
        , fwFlapBaseFreqDh = 32
        , fwProfile = (fwProfile defaultFirmwareParams)
            { profThrottleFrequencyMix = 100, profAileronSkewMix = 100
            , profRudderAmplitudeDiff = 35, profRudderFerocityRange = 20
            , profAileronScale = 60, profElevatorScale = 55 }
        }
      context = FwContext flightParams servo battery defaultFirmwareVehicleState 1.0
  castStablePtrToPtr <$> (newIORef context >>= newStablePtr)

b2f_math_destroy_firmware_vehicle :: Ptr () -> IO ()
b2f_math_destroy_firmware_vehicle pointer
  | pointer == nullPtr = pure ()
  | otherwise = freeStablePtr (castPtrToStablePtr pointer :: StablePtr (IORef FwContext))

b2f_math_set_stabilization :: Ptr () -> Word32 -> IO Int32
b2f_math_set_stabilization contextPointer enabled
  | contextPointer == nullPtr = pure 0
  | otherwise = do
      contextRef <- deRefStablePtr (castPtrToStablePtr contextPointer :: StablePtr (IORef FwContext))
      context <- readIORef contextRef
      writeIORef contextRef context
        { fwcState = (fwcState context) { fvStabilized = enabled /= 0 } }
      pure 1

b2f_math_set_wind_phase_noise :: Ptr () -> Double -> IO Int32
b2f_math_set_wind_phase_noise contextPointer noiseRadS
  | contextPointer == nullPtr = pure 0
  | otherwise = do
      contextRef <- deRefStablePtr (castPtrToStablePtr contextPointer :: StablePtr (IORef FwContext))
      context <- readIORef contextRef
      writeIORef contextRef context
        { fwcState = (fwcState context) { fvWindPhaseNoise = noiseRadS } }
      pure 1

-- | Apply a complete live tuning profile to an existing firmware-vehicle
-- context. Servo, battery and the exposed controller knobs are swapped in
-- place; the loop state (oscillator, strips, envelope, resonance) is kept, so
-- edits are smooth and take effect on the next step.
b2f_math_reconfigure_firmware_vehicle :: Ptr () -> Ptr () -> IO Int32
b2f_math_reconfigure_firmware_vehicle contextPointer tuningPointer
  | contextPointer == nullPtr || tuningPointer == nullPtr = pure 0
  | otherwise = run `catch` failure
  where
    run = do
      tuning <- peekTuning tuningPointer
      contextRef <- deRefStablePtr (castPtrToStablePtr contextPointer :: StablePtr (IORef FwContext))
      context <- readIORef contextRef
      writeIORef contextRef (applyTuning tuning context)
      pure 1
    failure :: SomeException -> IO Int32
    failure _ = pure 0

peekTuning :: Ptr () -> IO TuningConfig
peekTuning pointer = do
  values <- mapM (peekElemOff (castPtr pointer :: Ptr CDouble)) [0 .. 13]
  case map (\(CDouble value) -> value) values of
    [speed, stall, backdrive, voltage, resistance, capacity, freq, tailElev, glide, ferocity, aileron, elevator, mount, mass] ->
      pure (TuningConfig speed stall backdrive voltage resistance capacity freq tailElev glide ferocity aileron elevator mount mass)
    _ -> error "unreachable tuning layout"

applyTuning :: TuningConfig -> FwContext -> FwContext
applyTuning t ctx =
  let servo = ServoSpec
        { servoNoLoadSpeedDegPerSec = max 1 (tgServoSpeed t)
        , servoStallTorqueNm = max 0 (tgStallTorque t)
        , servoBackdriveDegPerSecNm = max 0 (tgBackdrive t)
        }
      battery = (fwcBattery ctx)
        { batteryNominalVoltage = max 0 (tgBatteryVoltage t)
        , batteryInternalResistanceOhm = max 0 (tgBatteryResistance t)
        , batteryCapacityAh = max 0 (tgBatteryCapacity t)
        }
      profile = (fwProfile (fwcParams ctx))
        { profTailElevatorAngleDeg = clampRange (-15) 15 (tgTailElevatorAngleDeg t)
        , profGlideAngleDeg = clampRange (-15) 15 (tgGlideAngleDeg t)
        , profStrokeFerocity = clampRange 0 100 (tgStrokeFerocity t)
        , profAileronScale = clampRange 0 100 (tgAileronScale t)
        , profElevatorScale = clampRange 0 100 (tgElevatorScale t)
        }
      -- Size the wing to the bird. Reference: 450 g → scale 1.0 (the default
      -- 1.44 m-span bird). A MILDER m^(1/4) scaling keeps a 10–25 g micro bird
      -- at ≥0.4× span (area ≥0.16×) instead of collapsing to 0.25× — so its
      -- wings still generate real lift/thrust, while the hinge torque
      -- (∝ wingScale³) still drops enough for a real micro-servo to drive them.
      wingScale = clampRange 0.4 1.8 (((max 0.01 (tgBodyMassKg t)) / 0.45) ** (1 / 4))
      -- Flapping frequency rises for lighter birds (f ∝ m^(-1/4) = 1/wingScale,
      -- the bird-like allometric trend). But it must be CAPPED by the servo's
      -- no-load speed: a servo sweeping at ω₀ can only flap a stroke of
      -- amplitude A at cadence f ≤ ω₀/(2π·A). Demanding the full 55° geometric
      -- stroke at a light bird's natural cadence collapsed the actuator — a
      -- 667°/s servo at 5.57 Hz tracked only 6° of the commanded stroke and
      -- produced NEGATIVE cruise thrust (measured). We cap the cadence so the
      -- servo always sweeps a USEFUL stroke (35°); a faster servo then earns a
      -- faster flap, which is exactly the "servo power matters" behaviour.
      freqScale = 1 / wingScale
      servoSpeedDegS = max 1 (servoNoLoadSpeedDegPerSec servo)
      usefulStrokeDeg = 35
      freqCapHz = servoSpeedDegS / (2 * pi * usefulStrokeDeg)
      flapBaseFreqDh = clampRange 10 200 (min (tgFlapBaseFreqDh t * freqScale)
                                             (freqCapHz * 10))
      params = (fwcParams ctx)
        { fwServoSpeedMs = 60000 / servoSpeedDegS
        , fwFlapBaseFreqDh = flapBaseFreqDh
        , fwMountIncidenceDeg = clampRange (-15) 15 (tgMountAngleDeg t)
        , fwProfile = profile
        }
  in ctx { fwcServo = servo, fwcBattery = battery, fwcParams = params, fwcWingScale = wingScale }

clampRange :: Double -> Double -> Double -> Double
clampRange lo hi = max lo . min hi

b2f_math_step_firmware_vehicle :: Ptr () -> Ptr () -> Ptr () -> Ptr () -> IO Int32
b2f_math_step_firmware_vehicle contextPointer pilotPointer bodyPointer outputPointer
  | contextPointer == nullPtr || pilotPointer == nullPtr
      || bodyPointer == nullPtr || outputPointer == nullPtr = pure 0
  | otherwise = run `catch` failure
  where
    run = do
      contextRef <- deRefStablePtr (castPtrToStablePtr contextPointer :: StablePtr (IORef FwContext))
      context <- readIORef contextRef
      pilot <- peekPilot pilotPointer
      body <- peekBody bodyPointer
      if all (\v -> not (isNaN v || isInfinite v))
           [piThrottle pilot, piRoll pilot, piPitch pilot, piYaw pilot]
        then pure () else ioError (userError "nonfinite pilot input")
      let rc = pilotToRc pilot
          (output, nextState) = stepFirmwareVehicle (piCoupled pilot) rc (fwcParams context)
                                  (fwcServo context) (fwcBattery context) (fwcWingScale context)
                                  (bodyDelta body) (bodyVel body) (bodyRates body) (fwcState context)
      if outputFlags output /= 0
        then pure 0
        else do
          -- Force/marshal before committing: an exception must not poison the context.
          pokeFwOutput outputPointer output nextState
          writeIORef contextRef context { fwcState = nextState }
          pure 1
    failure :: SomeException -> IO Int32
    failure _ = pure 0

peekConfig :: Ptr () -> IO FwConfig
peekConfig pointer = do
  values <- mapM (peekElemOff (castPtr pointer :: Ptr CDouble)) [0 .. 5]
  case map (\(CDouble value) -> value) values of
    [speed, stall, backdrive, voltage, resistance, capacity] ->
      pure (FwConfig speed stall backdrive voltage resistance capacity)
    _ -> pure (FwConfig 0 0 0 0 0 0)

peekPilot :: Ptr () -> IO PilotInput
peekPilot pointer = do
  values <- mapM (peekElemOff (castPtr pointer :: Ptr CDouble)) [0 .. 5]
  case map (\(CDouble value) -> value) values of
    [throttle, roll, pitch, yaw, speedMod, coupled] ->
      pure (PilotInput throttle roll pitch yaw (clampRange 0 1 speedMod) (coupled >= 0.5))
    _ -> pure defaultPilotInput

data BodyState = BodyState
  { bodyDelta :: !Double
  , bodyVel :: !Vec3
  , bodyRates :: !Vec3
  }

peekBody :: Ptr () -> IO BodyState
peekBody pointer = do
  values <- mapM (peekElemOff (castPtr pointer :: Ptr CDouble)) [0 .. 6]
  case map (\(CDouble value) -> value) values of
    [dt, lvx, lvy, lvz, avx, avy, avz] ->
      pure (BodyState dt (Vec3 lvx lvy lvz) (Vec3 avx avy avz))
    _ -> pure (BodyState 0 zeroVec zeroVec)

pokeFwOutput :: Ptr () -> VehicleOutput -> FirmwareVehicleState -> IO ()
pokeFwOutput pointer output state = do
  let Vec3 fx fy fz = totalForceN output
      Vec3 mx my mz = totalMomentNm output
      leftFlap = servoAngleDeg (fvServoLeft state)
      rightFlap = servoAngleDeg (fvServoRight state)
      soc = fvBatterySoc state
      pe = fvPhaseEnvelope state
      res = fvResonance state
      isFlapping = fwWasFlapping (fvFirmware state)
      doubles = [fx, fy, fz, mx, my, mz, totalMechanicalPowerW output
                , maxSeparation output, leftFlap, rightFlap, soc
                , phaseEnvelope pe, phaseCoverage pe
                , rsPhaseError res, fwKGainMod (fvFirmware state)]
      doublePointer = castPtr pointer :: Ptr CDouble
  sequence_ [pokeElemOff doublePointer index (CDouble value) | (index, value) <- zip [0 ..] doubles]
  let flags = if isFlapping then 1 else 0 :: Word32
  pokeByteOff pointer 120 (CUInt flags)

posOr :: Double -> Double -> Double
posOr fallback value = if value > 0 then value else fallback