{-# LANGUAGE ForeignFunctionInterface #-}

module Born2Flap.Math.FFI where

import Born2Flap.Math.Types (Vec3(..))
import Born2Flap.Math.Vehicle
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

b2f_math_abi_version :: IO Word32
b2f_math_abi_version = pure 2

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
  { fwcParams  :: !FirmwareParams
  , fwcServo   :: !ServoSpec
  , fwcBattery :: !BatterySpec
  , fwcState   :: !FirmwareVehicleState
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
            { profThrottleFrequencyMix = 100, profAileronSkewMix = 55
            , profRudderAmplitudeDiff = 35, profRudderFerocityRange = 20
            , profAileronScale = 25, profElevatorScale = 18 }
        }
      context = FwContext flightParams servo battery defaultFirmwareVehicleState
  castStablePtrToPtr <$> (newIORef context >>= newStablePtr)

b2f_math_destroy_firmware_vehicle :: Ptr () -> IO ()
b2f_math_destroy_firmware_vehicle pointer
  | pointer == nullPtr = pure ()
  | otherwise = freeStablePtr (castPtrToStablePtr pointer :: StablePtr (IORef FwContext))

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
          (output, nextState) = stepFirmwareVehicle rc (fwcParams context) (fwcServo context)
                                  (fwcBattery context) (bodyDelta body) (bodyVel body)
                                  (bodyRates body) (fwcState context)
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
  values <- mapM (peekElemOff (castPtr pointer :: Ptr CDouble)) [0 .. 3]
  case map (\(CDouble value) -> value) values of
    [throttle, roll, pitch, yaw] -> pure (PilotInput throttle roll pitch yaw)
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
      isFlapping = fwWasFlapping (fvFirmware state)
      doubles = [fx, fy, fz, mx, my, mz, totalMechanicalPowerW output
                , maxSeparation output, leftFlap, rightFlap, soc]
      doublePointer = castPtr pointer :: Ptr CDouble
  sequence_ [pokeElemOff doublePointer index (CDouble value) | (index, value) <- zip [0 ..] doubles]
  let flags = if isFlapping then 1 else 0 :: Word32
  pokeByteOff pointer 88 (CUInt flags)

posOr :: Double -> Double -> Double
posOr fallback value = if value > 0 then value else fallback
