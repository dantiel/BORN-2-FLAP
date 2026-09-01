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
    LibraryHandle = FPlatformProcess::GetDllHandle(*LibraryPath);
    if (!LibraryHandle)
    {
        Status = FString::Printf(TEXT("Haskell math backend missing: %s"), *LibraryPath);
        return false;
    }

    const AbiVersionFn AbiVersion = LoadExport<AbiVersionFn>(LibraryHandle, TEXT("b2f_math_abi_version"));
    const RuntimeInitFn RuntimeInit = LoadExport<RuntimeInitFn>(LibraryHandle, TEXT("b2f_math_runtime_init"));
    RuntimeShutdown = LoadExport<RuntimeShutdownFn>(LibraryHandle, TEXT("b2f_math_runtime_shutdown"));
    const CreateVehicleFn CreateVehicle = LoadExport<CreateVehicleFn>(LibraryHandle, TEXT("b2f_math_create_default_vehicle"));
    DestroyVehicle = LoadExport<DestroyVehicleFn>(LibraryHandle, TEXT("b2f_math_destroy_vehicle"));
    StepVehicle = LoadExport<StepVehicleFn>(LibraryHandle, TEXT("b2f_math_step_vehicle"));

    if (!AbiVersion || !RuntimeInit || !RuntimeShutdown || !CreateVehicle || !DestroyVehicle || !StepVehicle)
    {
        Status = TEXT("Haskell math backend has an incomplete C ABI");
        Unload();
        return false;
    }
    if (AbiVersion() != B2F_MATH_ABI_VERSION)
    {
        Status = TEXT("Haskell math backend ABI mismatch");
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

    Context = CreateVehicle();
    if (!Context)
    {
        Status = TEXT("Haskell math backend could not create a vehicle context");
        Unload();
        return false;
    }

    Status = TEXT("Haskell math backend ready");
    return true;
}

void FBorn2FlapMathBridge::Unload()
{
    if (Context && DestroyVehicle)
    {
        DestroyVehicle(Context);
    }
    Context = nullptr;
    if (bRuntimeInitialized && LibraryHandle && RuntimeShutdown)
    {
        RuntimeShutdown();
    }
    bRuntimeInitialized = false;
    RuntimeShutdown = nullptr;
    DestroyVehicle = nullptr;
    StepVehicle = nullptr;
    if (LibraryHandle)
    {
        FPlatformProcess::FreeDllHandle(LibraryHandle);
        LibraryHandle = nullptr;
    }
}

bool FBorn2FlapMathBridge::Step(const B2F_VehicleInput& Input, B2F_VehicleOutput& Output) const
{
    return Context && StepVehicle && StepVehicle(Context, &Input, &Output) != 0;
}
