#include "Math/Born2FlapMathBridge.h"

#include "HAL/PlatformProcess.h"
#include "Misc/Paths.h"

namespace
{
FString MathLibraryName()
{
#if PLATFORM_WINDOWS
    return TEXT("born2flap_math.dll");
#elif PLATFORM_MAC
    return TEXT("libborn2flap_math.dylib");
#else
    return TEXT("libborn2flap_math.so");
#endif
}

template <typename FunctionType>
FunctionType LoadExport(void* Handle, const TCHAR* Name)
{
    return reinterpret_cast<FunctionType>(FPlatformProcess::GetDllExport(Handle, Name));
}
}

FBorn2FlapMathBridge::FBorn2FlapMathBridge() = default;

FBorn2FlapMathBridge::~FBorn2FlapMathBridge()
{
    Unload();
}

bool FBorn2FlapMathBridge::Load()
{
    Unload();
    const FString LibraryPath = FPaths::Combine(
        FPaths::ProjectDir(), TEXT("Binaries"), TEXT("ThirdParty"), MathLibraryName());
    // Pin one loader reference for the process: GHC cannot be stopped/restarted
    // between PIE sessions. Per-aircraft contexts are still freed by Unload().
    static void* ProcessLibraryHandle = nullptr;
    if (!ProcessLibraryHandle)
    {
        ProcessLibraryHandle = FPlatformProcess::GetDllHandle(*LibraryPath);
    }
    LibraryHandle = ProcessLibraryHandle;
    if (!LibraryHandle)
    {
        Status = FString::Printf(TEXT("Haskell math backend missing: %s"), *LibraryPath);
        UE_LOG(LogTemp, Error, TEXT("%s"), *Status);
        return false;
    }

    const AbiVersionFn AbiVersion = LoadExport<AbiVersionFn>(LibraryHandle, TEXT("b2f_math_abi_version"));
    const RuntimeInitFn RuntimeInit = LoadExport<RuntimeInitFn>(LibraryHandle, TEXT("b2f_math_runtime_init"));
    RuntimeShutdown = LoadExport<RuntimeShutdownFn>(LibraryHandle, TEXT("b2f_math_runtime_shutdown"));
    const CreateFirmwareVehicleFn CreateFirmwareVehicle =
        LoadExport<CreateFirmwareVehicleFn>(LibraryHandle, TEXT("b2f_math_create_firmware_vehicle"));
    DestroyFirmwareVehicle =
        LoadExport<DestroyFirmwareVehicleFn>(LibraryHandle, TEXT("b2f_math_destroy_firmware_vehicle"));
    StepFirmwareVehicle =
        LoadExport<StepFirmwareVehicleFn>(LibraryHandle, TEXT("b2f_math_step_firmware_vehicle"));

    if (!AbiVersion || !RuntimeInit || !RuntimeShutdown || !CreateFirmwareVehicle ||
        !DestroyFirmwareVehicle || !StepFirmwareVehicle)
    {
        Status = TEXT("Haskell math backend has an incomplete C ABI");
        UE_LOG(LogTemp, Error, TEXT("%s"), *Status);
        Unload();
        return false;
    }
    bRuntimeInitialized = RuntimeInit() != 0;
    if (!bRuntimeInitialized)
    {
        Status = TEXT("Haskell runtime initialization failure");
        Unload();
        return false;
    }

    if (AbiVersion() != B2F_MATH_ABI_VERSION)
    {
        Status = TEXT("Haskell math backend ABI mismatch");
        Unload();
        return false;
    }

    // Prototype flight hardware, in wing-hinge units: the former 2 Nm drive
    // stalled under the 1.44 m wing's load. This is a provisional vehicle setup,
    // not a force multiplier. The firmware still feels every aerodynamic load.
    B2F_FirmwareConfig FlightConfig{};
    FlightConfig.servo_no_load_speed_deg_s = 1200;
    FlightConfig.servo_stall_torque_nm = 8;
    FlightConfig.servo_backdrive_deg_s_nm = 20;
    FlightConfig.battery_voltage = 11.1;
    FlightConfig.battery_resistance_ohm = .08;
    FlightConfig.battery_capacity_ah = 1.3;
    Context = CreateFirmwareVehicle(&FlightConfig);
    if (!Context)
    {
        Status = TEXT("Haskell math backend could not create a firmware vehicle context");
        Unload();
        return false;
    }

    Status = TEXT("Haskell firmware backend ready");
    UE_LOG(LogTemp, Display, TEXT("%s: %s"), *Status, *LibraryPath);
    return true;
}

void FBorn2FlapMathBridge::Unload()
{
    if (Context && DestroyFirmwareVehicle)
    {
        DestroyFirmwareVehicle(Context);
    }
    Context = nullptr;
    if (bRuntimeInitialized && LibraryHandle && RuntimeShutdown)
    {
        RuntimeShutdown();
    }
    bRuntimeInitialized = false;
    RuntimeShutdown = nullptr;
    DestroyFirmwareVehicle = nullptr;
    StepFirmwareVehicle = nullptr;
    if (LibraryHandle)
    {
        // The process owns the loader reference, not this aircraft.
        LibraryHandle = nullptr;
    }
}

bool FBorn2FlapMathBridge::Step(
    const B2F_PilotInput& Pilot, const B2F_BodyState& Body, B2F_FirmwareOutput& Output) const
{
    return Context && StepFirmwareVehicle &&
        StepFirmwareVehicle(Context, &Pilot, &Body, &Output) != 0;
}
