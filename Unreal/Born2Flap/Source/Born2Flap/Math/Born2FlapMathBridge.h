#pragma once

#include "CoreMinimal.h"
#include "born2flap_math.h"

class FBorn2FlapMathBridge final
{
public:
    FBorn2FlapMathBridge();
    ~FBorn2FlapMathBridge();

    FBorn2FlapMathBridge(const FBorn2FlapMathBridge&) = delete;
    FBorn2FlapMathBridge& operator=(const FBorn2FlapMathBridge&) = delete;

    bool Load();
    void Unload();
    bool IsReady() const { return Context != nullptr; }
    bool Step(const B2F_VehicleInput& Input, B2F_VehicleOutput& Output) const;
    const FString& GetStatus() const { return Status; }

private:
    using AbiVersionFn = uint32_t (*)();
    using RuntimeInitFn = int32_t (*)();
    using RuntimeShutdownFn = void (*)();
    using CreateVehicleFn = B2F_MathContext* (*)();
    using DestroyVehicleFn = void (*)(B2F_MathContext*);
    using StepVehicleFn = int32_t (*)(B2F_MathContext*, const B2F_VehicleInput*, B2F_VehicleOutput*);

    void* LibraryHandle = nullptr;
    B2F_MathContext* Context = nullptr;
    bool bRuntimeInitialized = false;
    RuntimeShutdownFn RuntimeShutdown = nullptr;
    DestroyVehicleFn DestroyVehicle = nullptr;
    StepVehicleFn StepVehicle = nullptr;
    FString Status = TEXT("Math backend not loaded");
};
