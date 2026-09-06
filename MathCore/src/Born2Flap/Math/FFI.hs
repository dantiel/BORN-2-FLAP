{-# LANGUAGE ForeignFunctionInterface #-}

module Born2Flap.Math.FFI where

import Born2Flap.Math.Types (Vec3(..))
import Born2Flap.Math.Vehicle
import Control.Exception (SomeException, catch)
import Data.IORef
import Data.Int (Int32)
import Data.Word (Word32)
import Foreign.C.Types (CDouble(..), CInt(..), CUInt(..))
import Foreign.Ptr
import Foreign.StablePtr
import Foreign.Storable

foreign export ccall b2f_math_abi_version :: IO Word32
foreign export ccall b2f_math_runtime_init :: IO Int32
foreign export ccall b2f_math_runtime_shutdown :: IO ()
foreign export ccall b2f_math_create_default_vehicle :: IO (Ptr ())
foreign export ccall b2f_math_destroy_vehicle :: Ptr () -> IO ()
foreign export ccall b2f_math_step_vehicle :: Ptr () -> Ptr () -> Ptr () -> IO Int32

b2f_math_abi_version :: IO Word32
b2f_math_abi_version = pure 1

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
