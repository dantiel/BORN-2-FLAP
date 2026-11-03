{-# LANGUAGE ForeignFunctionInterface #-}

module Born2Flap.Math.FFI where

import Born2Flap.Math.Types (Vec3(..))
import Born2Flap.Math.Airframe
import Born2Flap.Math.Vehicle
import Born2Flap.Math.LiftField (PressureField(..))
import Born2Flap.Math.Model (WingModel(..), kestrelModel, kestrelLightModel, ravenModel, prototypeModel, turtleModel)
import Born2Flap.Math.Planform (shapeChord, shapeTwistRad)
import Born2Flap.Math.PhaseEnvelope (phaseEnvelope, phaseCoverage)
import Born2Flap.Math.ResponsePhase (rpPhaseError)
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
foreign export ccall "hs_b2f_math_get_wing_loads" b2f_math_get_wing_loads :: Ptr () -> Word32 -> Ptr () -> Ptr () -> IO Int32
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
foreign export ccall "hs_b2f_math_set_wing_model" b2f_math_set_wing_model :: Ptr () -> Word32 -> IO Int32
foreign export ccall "hs_b2f_math_get_model_mass_kg" b2f_math_get_model_mass_kg :: Word32 -> IO Double
foreign export ccall "hs_b2f_math_get_wing_shape" b2f_math_get_wing_shape :: Ptr () -> Word32 -> Ptr () -> Ptr () -> IO Int32
foreign export ccall "hs_b2f_math_get_wing_forces" b2f_math_get_wing_forces :: Ptr () -> Word32 -> Ptr () -> Ptr () -> IO Int32
foreign export ccall "hs_b2f_math_get_wing_fields" b2f_math_get_wing_fields :: Ptr () -> Word32 -> Ptr () -> Ptr () -> IO Int32
foreign export ccall "hs_b2f_math_set_strip_wind" b2f_math_set_strip_wind :: Ptr () -> Word32 -> Ptr () -> Ptr () -> IO Int32

foreign export ccall "hs_b2f_math_transport_airfields" b2f_math_transport_airfields :: Ptr () -> Ptr () -> IO Int32
foreign export ccall "hs_b2f_math_get_tail_incidence" b2f_math_get_tail_incidence :: Ptr () -> Ptr () -> IO Int32
foreign export ccall "hs_b2f_math_get_tail_fields" b2f_math_get_tail_fields :: Ptr () -> Word32 -> Ptr () -> IO Int32
foreign export ccall "hs_b2f_math_get_external_force" b2f_math_get_external_force :: Ptr () -> Ptr () -> IO Int32
foreign export ccall "hs_b2f_math_set_mass_frame" b2f_math_set_mass_frame :: Ptr () -> Double -> Double -> Double -> IO Int32

-- Enable simultaneous angular force evaluation using the host's actual tensor.
-- Existing prescribed-pitch/yaw wind tunnels leave this extension unset.
b2f_math_set_mass_frame :: Ptr () -> Double -> Double -> Double -> IO Int32
b2f_math_set_mass_frame pointer cgX iy iz
  | pointer == nullPtr || any (\v -> isNaN v || isInfinite v) [cgX,iy,iz]
      || abs cgX > 1 || iy <= 0 || iz <= 0 = pure 0
  | otherwise = run `catch` failure
  where
    run = do
      ref <- deRefStablePtr (castPtrToStablePtr pointer :: StablePtr (IORef FwContext))
      modifyIORef' ref (\context -> context { fwcState = (fwcState context)
        { fvMassFrame = Just (cgX,iy,iz), fvTangent = Nothing } })
      pure 1
    failure :: SomeException -> IO Int32
    failure _ = pure 0

-- Host-equivalent force also contains added-mass, recoil and gravity terms;
-- those are not surface forces and must not be moved to an aerodynamic arm.
b2f_math_get_external_force :: Ptr () -> Ptr () -> IO Int32
b2f_math_get_external_force pointer output
  | pointer == nullPtr || output == nullPtr = pure 0
  | otherwise = run `catch` failure
  where
    run = do
      ref <- deRefStablePtr (castPtrToStablePtr pointer :: StablePtr (IORef FwContext))
      context <- readIORef ref
      let Vec3 fx fy fz = fvExternalForce (fwcState context)
          values = [fx,fy,fz]
      if any (\v -> isNaN v || isInfinite v) values then pure 0 else do
        sequence_ [pokeElemOff (castPtr output :: Ptr CDouble) i (CDouble v) | (i,v) <- zip [0..] values]
        pure 1
    failure :: SomeException -> IO Int32
    failure _ = pure 0

b2f_math_get_tail_fields :: Ptr () -> Word32 -> Ptr () -> IO Int32
b2f_math_get_tail_fields pointer capacity output
  | pointer==nullPtr || output==nullPtr || capacity<18 = pure 0
  | otherwise = run `catch` failure
 where
  run = do
    ref <- deRefStablePtr (castPtrToStablePtr pointer :: StablePtr (IORef FwContext))
    context <- readIORef ref
    let fields = fvTailFields (fwcState context)
        xyz (Vec3 a b c) = [a,b,c]
        values = concatMap (\f -> concatMap xyz [tfPosition f,tfNormal f,tfVelocity f,
                    tfLift f,tfPressure f,tfDrag f,tfMoment f]) fields
    if length fields/=18 || not(all (\v -> not(isNaN v || isInfinite v)) values) then pure 0 else do
      sequence_ [pokeElemOff (castPtr output :: Ptr CDouble) i (CDouble v) | (i,v)<-zip [0..] values]
      pure 18
  failure :: SomeException -> IO Int32
  failure _ = pure 0

-- Read the angle used for the common tail force; the host must not duplicate
-- the controller's throttle/pitch mix in its animation.
b2f_math_get_tail_incidence :: Ptr () -> Ptr () -> IO Int32
b2f_math_get_tail_incidence pointer output
  | pointer==nullPtr || output==nullPtr = pure 0
  | otherwise = run `catch` failure
 where
  run = do
    ref <- deRefStablePtr (castPtrToStablePtr pointer :: StablePtr (IORef FwContext))
    context <- readIORef ref
    let angle=if wmName (fwcModel context) `elem` ["kestrel", "kestrel-light"]
              then profTailElevatorAngleDeg (fwProfile (fwcParams context))
              else fvTailIncidenceDeg(fwcState context)
    if isNaN angle || isInfinite angle then pure 0 else do
      poke (castPtr output :: Ptr CDouble) (CDouble angle)
      pure 1
  failure :: SomeException -> IO Int32
  failure _ = pure 0

-- Correct the predicted body basis to the authoritative host attitude. This
-- re-expresses field directions and fixed air anchors in the new body basis;
-- it does not change wing geometry or motion.
b2f_math_transport_airfields :: Ptr () -> Ptr () -> IO Int32
b2f_math_transport_airfields pointer rotationPointer
  | pointer==nullPtr || rotationPointer==nullPtr = pure 0
  | otherwise = run `catch` failure
 where
  run = do
    values <- mapM (peekElemOff (castPtr rotationPointer :: Ptr CDouble)) [0..2]
    let vector=case values of [CDouble a,CDouble b,CDouble c] -> Vec3 a b c; _ -> zeroVec
        size=magnitude vector
    if isNaN size || isInfinite size then pure 0 else do
      ref <- deRefStablePtr (castPtrToStablePtr pointer :: StablePtr (IORef FwContext))
      context <- readIORef ref
      let next=transportAirfields vector (fwcState context)
      writeIORef ref (context {fwcState=next})
      pure 1
  failure :: SomeException -> IO Int32
  failure _ = pure 0

-- Read the same solved per-strip force decomposition used by the mechanics.
-- Validate both arrays before writing either; diagnostics never advance state.
b2f_math_get_wing_loads :: Ptr () -> Word32 -> Ptr () -> Ptr () -> IO Int32
b2f_math_get_wing_loads pointer capacity leftPointer rightPointer
  | pointer == nullPtr || leftPointer == nullPtr || rightPointer == nullPtr
      || capacity < fromIntegral stripCount = pure 0
  | otherwise = run `catch` failure
 where
  run = do
    ref <- deRefStablePtr (castPtrToStablePtr pointer :: StablePtr (IORef FwContext))
    context <- readIORef ref
    let state = fwcState context
        xyz (Vec3 a b c) = [a,b,c]
        values = concatMap (\s -> concatMap xyz
          [addVec (stripLift s) (addVec (stripDrag s) (stripPressure s)), stripDrag s, stripMoment s])
        left = values (fvLeftStrips state)
        right = values (fvRightStrips state)
        valid xs = length xs == stripCount*9 && all (\v -> not (isNaN v || isInfinite v)) xs
        write p xs = sequence_ [pokeElemOff (castPtr p :: Ptr CDouble) i (CDouble v) | (i,v)<-zip [0..] xs]
    if not (valid left && valid right) then pure 0 else do
      write leftPointer left
      write rightPointer right
      pure (fromIntegral stripCount)
  failure :: SomeException -> IO Int32
  failure _ = pure 0

-- Additive ABI-16 extensions: existing input/output layouts are unchanged.
b2f_math_get_wing_fields :: Ptr () -> Word32 -> Ptr () -> Ptr () -> IO Int32
b2f_math_get_wing_fields pointer capacity leftPointer rightPointer
  | pointer == nullPtr || leftPointer == nullPtr || rightPointer == nullPtr
      || capacity < fromIntegral stripCount = pure 0
  | otherwise = run `catch` failure
 where
  run = do
    ref <- deRefStablePtr (castPtrToStablePtr pointer :: StablePtr (IORef FwContext))
    context <- readIORef ref
    let state = fwcState context
        xyz (Vec3 a b c) = [a,b,c]
        values = concatMap (\s -> concatMap xyz
          [stripPosition s, stripNormal s, stripLift s, stripPressure s,
           stripAirVelocity s, fieldDisplacement (stripPressureField s),
           fieldDirection (stripPressureField s)])
        left = values (fvLeftStrips state)
        right = values (fvRightStrips state)
        valid xs = length xs == stripCount*21 && all (\v -> not (isNaN v || isInfinite v)) xs
        write p xs = sequence_ [pokeElemOff (castPtr p :: Ptr CDouble) i (CDouble v) | (i,v)<-zip [0..] xs]
    if not (valid left && valid right) then pure 0 else do
      write leftPointer left
      write rightPointer right
      pure (fromIntegral stripCount)
  failure :: SomeException -> IO Int32
  failure _ = pure 0

-- Wind increments relative to the wind already subtracted at the body origin.
-- Validate BOTH arrays before committing any state.
b2f_math_set_strip_wind :: Ptr () -> Word32 -> Ptr () -> Ptr () -> IO Int32
b2f_math_set_strip_wind pointer count leftPointer rightPointer
  | pointer == nullPtr || leftPointer == nullPtr || rightPointer == nullPtr
      || count /= fromIntegral stripCount = pure 0
  | otherwise = run `catch` failure
 where
  run = do
    left <- mapM (peekElemOff (castPtr leftPointer :: Ptr CDouble)) [0..stripCount*3-1]
    right <- mapM (peekElemOff (castPtr rightPointer :: Ptr CDouble)) [0..stripCount*3-1]
    let values = map (\(CDouble v) -> v)
        finite v = not (isNaN v || isInfinite v)
        vectors (a:b:c:rest) = Vec3 a b c : vectors rest
        vectors [] = []
        vectors _ = error "invalid wind vector array"
    if not (all finite (values (left++right))) then pure 0 else do
      ref <- deRefStablePtr (castPtrToStablePtr pointer :: StablePtr (IORef FwContext))
      context <- readIORef ref
      let state = fwcState context
          update wind strips = zipWith (\w s -> s {stripWindDelta=w}) (vectors (values wind)) strips
          next = state {fvLeftStrips=update left (fvLeftStrips state), fvRightStrips=update right (fvRightStrips state)}
      writeIORef ref (context {fwcState=next})
      pure 1
  failure :: SomeException -> IO Int32
  failure _ = pure 0

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
          wp = wmWing (fwcModel context)
          values strips = concat
            [ let fraction = (fromIntegral i + 0.5) / fromIntegral stripCount
              in [fraction, shapeChord wp fraction, stripBendM s,
                  shapeTwistRad wp fraction, stripTwistAero s, stripCamber s]
            | (i, s) <- zip [0 :: Int ..] strips ]
          left = values (fvLeftStrips state)
          right = values (fvRightStrips state)
          valid xs = length xs == stripCount * 6 && all (\v -> not (isNaN v || isInfinite v)) xs
          write pointer xs = sequence_
            [pokeElemOff (castPtr pointer :: Ptr CDouble) i (CDouble v) | (i,v) <- zip [0..] xs]
      if not (valid left && valid right) then pure 0 else do
        write leftPointer left
        write rightPointer right
        pure (fromIntegral stripCount)
    failure :: SomeException -> IO Int32
    failure _ = pure 0
b2f_math_get_wing_forces :: Ptr () -> Word32 -> Ptr () -> Ptr () -> IO Int32
b2f_math_get_wing_forces contextPointer capacity leftPointer rightPointer
  | contextPointer == nullPtr || leftPointer == nullPtr || rightPointer == nullPtr
      || capacity < fromIntegral stripCount = pure 0
  | otherwise = run `catch` failure
  where
    run = do
      contextRef <- deRefStablePtr (castPtrToStablePtr contextPointer :: StablePtr (IORef FwContext))
      context <- readIORef contextRef
      let state = fwcState context
          left = fvLeftStripForceMag state
          right = fvRightStripForceMag state
          valid xs = length xs == stripCount && all (\v -> not (isNaN v || isInfinite v)) xs
          write pointer xs = sequence_
            [pokeElemOff (castPtr pointer :: Ptr CDouble) i (CDouble v) | (i, v) <- zip [0 ..] xs]
      if not (valid left && valid right) then pure 0 else do
        write leftPointer left
        write rightPointer right
        pure (fromIntegral stripCount)
    failure :: SomeException -> IO Int32
    failure _ = pure 0
b2f_math_abi_version :: IO Word32
b2f_math_abi_version = pure 18

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
        , groundClearanceM = 1e9  -- legacy path carries no ground info
        , bodyUpB = Vec3 0 0 1    -- legacy path carries no attitude info
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
  , fwcModel      :: !WingModel  -- ^ the bird's physical model (wing + carbon rods)
  , fwcMass :: !Double
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
  , tgBatteryCRating    :: !Double  -- ^ continuous discharge rating (C), e.g. 30
  , tgBatteryBurstCRating :: !Double  -- ^ burst discharge rating (C), e.g. 60 = momentarily double
  , tgFlapBaseFreqDh    :: !Double  -- ^ flap frequency ceiling [deci-Hz, 10..200]
  , tgTailElevatorAngleDeg :: !Double  -- ^ physical tail mounting incidence [deg]
  , tgGlideAngleDeg     :: !Double  -- ^ glide incidence [deg]
  , tgStrokeFerocity    :: !Double  -- ^ downstroke ferocity [0..100]
  , tgAileronScale      :: !Double  -- ^ aileron mix [0..100]
  , tgElevatorScale     :: !Double  -- ^ elevator mix [0..100]
  , tgFlapCentreDeg     :: !Double  -- ^ flap stroke centre trim (dihedral), like tail elevator [deg]
  , tgBodyMassKg        :: !Double  -- ^ airframe mass [kg], scales the wing planform
  , tgVerticalMountAngleDeg :: !Double  -- ^ real wing mount incidence (nose-up +) [deg]
  , tgReturnFerocity     :: !Double  -- ^ upstroke/return ferocity [0..100] (ABI v15)
  , tgYawScale           :: !Double  -- ^ rudder amplitude differential (YAW SCALE) [0..100] (ABI v18)
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
            , profRudderAmplitudeDiff = 60, profRudderFerocityRange = 20
            , profAileronScale = 10, profElevatorScale = 55 }
        }
      context = FwContext flightParams servo battery defaultFirmwareVehicleState kestrelModel 0.050
  castStablePtrToPtr <$> (newIORef context >>= newStablePtr)

b2f_math_destroy_firmware_vehicle :: Ptr () -> IO ()
b2f_math_destroy_firmware_vehicle pointer
  | pointer == nullPtr = pure ()
  | otherwise = freeStablePtr (castPtrToStablePtr pointer :: StablePtr (IORef FwContext))

-- | LEGACY no-op (ABI-preserved). The former ONDAS "stabilized" master switch
-- gated a phase-lock resonance controller that has been removed: physical
-- resonance is now emergent from the wing's elastic spring model, so there is
-- no stabilization flag to set. The symbol is kept so the host ABI is stable.
b2f_math_set_stabilization :: Ptr () -> Word32 -> IO Int32
b2f_math_set_stabilization contextPointer _enabled
  | contextPointer == nullPtr = pure 0
  | otherwise = pure 1

-- | LEGACY no-op (ABI-preserved). Wind phase noise (eta) only fed the removed
-- phase-lock resonance controller; the read-only response-phase diagnostic
-- takes no injected noise, and the physical spring model is driven by real
-- aerodynamic forcing. The symbol is kept so the host can still link/call it.
b2f_math_set_wind_phase_noise :: Ptr () -> Double -> IO Int32
b2f_math_set_wind_phase_noise contextPointer _noiseRadS
  | contextPointer == nullPtr = pure 0
  | otherwise = pure 1

-- | Select the bird's physical model (wing outline + carbon-rod inventory) by
-- id. This is the per-model configuration hook: a second bird is a new
-- 'WingModel' value registered here, not a code change in the solver. Model 0
-- is the kestrel; unknown ids fall back to the kestrel. Additive ABI extension.
b2f_math_set_wing_model :: Ptr () -> Word32 -> IO Int32
b2f_math_set_wing_model contextPointer modelId
  | contextPointer == nullPtr = pure 0
  | otherwise = run `catch` failure
  where
    run = do
      contextRef <- deRefStablePtr (castPtrToStablePtr contextPointer :: StablePtr (IORef FwContext))
      context <- readIORef contextRef
      let model = modelById modelId
      writeIORef contextRef context { fwcModel = model, fwcMass = wmMassKg model }
      pure 1
    failure :: SomeException -> IO Int32
    failure _ = pure 0

modelById :: Word32 -> WingModel
modelById 0 = ravenModel       -- RavenCrow (nature valley)
modelById 1 = prototypeModel   -- Prototype (training arena)
modelById 2 = kestrelModel     -- Kestrel / falcon (Shiomori coast)
modelById 3 = turtleModel      -- Neon turtle (void level)
modelById 4 = kestrelLightModel -- Kestrel light (25 g, thinner rods, D43S servo)
modelById _ = kestrelModel     -- unknown ??? safe reference default

-- | A model's dry mass by id — the C++ host reads this to set the Chaos body
-- mass, so per-model mass lives with the model config, never as a C++ constant.
b2f_math_get_model_mass_kg :: Word32 -> IO Double
b2f_math_get_model_mass_kg = pure . wmMassKg . modelById

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
  values <- mapM (peekElemOff (castPtr pointer :: Ptr CDouble)) [0 .. 18]
  case map (\(CDouble value) -> value) values of
    [speed, stall, backdrive, voltage, resistance, capacity, cRating, burstRating, freq, tailElev, glide, ferocity, aileron, elevator, flapCentre, mass, verticalMount, returnFerocity, yawScale] ->
      pure (TuningConfig speed stall backdrive voltage resistance capacity cRating burstRating freq tailElev glide ferocity aileron elevator flapCentre mass verticalMount returnFerocity yawScale)
    _ -> error "unreachable tuning layout"

applyTuning :: TuningConfig -> FwContext -> FwContext
applyTuning t ctx =
  let servo = ServoSpec
        { servoNoLoadSpeedDegPerSec = max 1 (tgServoSpeed t)
        , servoStallTorqueNm = max 0 (tgStallTorque t)
        , servoBackdriveDegPerSecNm = max 0 (tgBackdrive t)
        }
      cRate = clampRange 1 200 (tgBatteryCRating t)
      battery = (fwcBattery ctx)
        { batteryNominalVoltage = max 0 (tgBatteryVoltage t)
        , batteryInternalResistanceOhm = max 0 (tgBatteryResistance t)
        , batteryCapacityAh = max 0 (tgBatteryCapacity t)
        , batteryCRating = cRate
        , batteryBurstCRating = max cRate (clampRange 1 400 (tgBatteryBurstCRating t))
        }
      profile = (fwProfile (fwcParams ctx))
        { profTailElevatorAngleDeg = clampRange (-15) 15 (tgTailElevatorAngleDeg t)
        , profGlideAngleDeg = clampRange (-15) 15 (tgGlideAngleDeg t)
        , profStrokeFerocity = clampRange 0 100 (tgStrokeFerocity t)
        , profReturnFerocity = clampRange 0 100 (tgReturnFerocity t)
        , profAileronScale = clampRange 0 100 (tgAileronScale t)
        , profElevatorScale = clampRange 0 100 (tgElevatorScale t)
        , profRudderAmplitudeDiff = clampRange 0 100 (tgYawScale t)
        }
      -- Preserve the requested frequency. The mixer limits cadence using the
      -- actual arc and waveform derivative, once all control mixes are known.
      -- Adding body mass does not change the requested motion or wing geometry.
      servoSpeedDegS = max 1 (servoNoLoadSpeedDegPerSec servo)
      flapBaseFreqDh = clampRange 10 200 (tgFlapBaseFreqDh t)
      params = (fwcParams ctx)
        { fwServoSpeedMs = 60000 / servoSpeedDegS
        , fwFlapBaseFreqDh = flapBaseFreqDh
        , fwFlapCentreDeg = clampRange (-15) 15 (tgFlapCentreDeg t)
        , fwVerticalMountIncidenceDeg = clampRange (-15) 15 (tgVerticalMountAngleDeg t)
        , fwProfile = profile
        }
  in ctx { fwcServo = servo, fwcBattery = battery, fwcParams = params, fwcMass = max 0.01 (tgBodyMassKg t), fwcState = (fwcState ctx) {fvTangent=Nothing} }

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
          (output, nextState) = stepFirmwareVehicleWithMassInertia (fwcMass context) (bodyRollInertia body) (piCoupled pilot) rc (fwcParams context)
                                  (fwcServo context) (fwcBattery context) (fwcModel context)
                                  (bodyDelta body) (bodyVel body) (bodyRates body)
                                  (bodyGroundClearance body) (bodyUp body) (fwcState context)
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
  , bodyRollInertia :: !Double
  , bodyGroundClearance :: !Double  -- ^ body-origin height above the surface below [m] (ABI v11)
  , bodyUp :: !Vec3                 -- ^ world-up in body frame; zero disables gravity coupling (ABI v13)
  }

peekBody :: Ptr () -> IO BodyState
peekBody pointer = do
  values <- mapM (peekElemOff (castPtr pointer :: Ptr CDouble)) [0 .. 11]
  case map (\(CDouble value) -> value) values of
    [dt, lvx, lvy, lvz, avx, avy, avz, ground, inertia, upx, upy, upz] ->
      pure (BodyState dt (Vec3 lvx lvy lvz) (Vec3 avx avy avz) inertia ground (Vec3 upx upy upz))
    _ -> pure (BodyState 0 zeroVec zeroVec 0 0 (Vec3 0 0 1))

pokeFwOutput :: Ptr () -> VehicleOutput -> FirmwareVehicleState -> IO ()
pokeFwOutput pointer output state = do
  let Vec3 fx fy fz = totalForceN output
      Vec3 mx my mz = totalMomentNm output
      leftFlap = servoAngleDeg (fvServoLeft state)
      rightFlap = servoAngleDeg (fvServoRight state)
      soc = fvBatterySoc state
      pe = fvPhaseEnvelope state
      rp = fvResponsePhase state
      isFlapping = fwWasFlapping (fvFirmware state)
      doubles = [fx, fy, fz, mx, my, mz, totalMechanicalPowerW output
                , maxSeparation output, leftFlap, rightFlap, soc
                , phaseEnvelope pe, phaseCoverage pe
                , rpPhaseError rp, fwKGainMod (fvFirmware state)
                , fvFlapHz state]
      doublePointer = castPtr pointer :: Ptr CDouble
  sequence_ [pokeElemOff doublePointer index (CDouble value) | (index, value) <- zip [0 ..] doubles]
  let flags = if isFlapping then 1 else 0 :: Word32
  pokeByteOff pointer 128 (CUInt flags)

posOr :: Double -> Double -> Double
posOr fallback value = if value > 0 then value else fallback
foreign export ccall "hs_b2f_math_get_mass_properties" b2f_math_get_mass_properties :: Ptr () -> Double -> Ptr () -> IO Int32
b2f_math_get_mass_properties :: Ptr () -> Double -> Ptr () -> IO Int32
b2f_math_get_mass_properties ctx cgX out
 | ctx == nullPtr || out == nullPtr || isNaN cgX || isInfinite cgX = pure 0
 | otherwise = run `catch` failure
 where
  run = do
   ref <- deRefStablePtr (castPtrToStablePtr ctx :: StablePtr (IORef FwContext))
   state <- readIORef ref
   let total=fwcMass state
       (body,wing,tailMass)=airframeMasses total
       (ix,iy,iz)=airframeInertia (fwcModel state) total cgX
       values=[total,body,wing,tailMass,ix,iy,iz]
   sequence_ [pokeElemOff (castPtr out :: Ptr CDouble) i (CDouble v) | (i,v) <- zip [0..] values]
   pure 1
  failure :: SomeException -> IO Int32
  failure _ = pure 0