#pragma once

#include "CoreMinimal.h"
#include "born2flap_math.h"

// Thin C++ wrapper around the Haskell math backend's firmware-emulation ABI.
// Owns the dlopen/dlsym plumbing and a single B2F_MathContext created with
// b2f_math_create_firmware_vehicle. Unreal stays runnable without the backend:
// Load() reports false and the pawn simply does not step the math.
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

    // One closed-loop firmware step: pilot sticks + body state in, forces +
    // flap angles + battery observables out.
    bool Step(const B2F_PilotInput& Pilot, const B2F_BodyState& Body, B2F_FirmwareOutput& Output) const;

    const FString& GetStatus() const { return Status; }

private:
    using AbiVersionFn = uint32_t (*)();
    using RuntimeInitFn = int32_t (*)();
    using RuntimeShutdownFn = void (*)();
    using CreateFirmwareVehicleFn = B2F_MathContext* (*)(const B2F_FirmwareConfig*);
    using DestroyFirmwareVehicleFn = void (*)(B2F_MathContext*);
    using StepFirmwareVehicleFn = int32_t (*)(
        B2F_MathContext*, const B2F_PilotInput*, const B2F_BodyState*, B2F_FirmwareOutput*);

    void* LibraryHandle = nullptr;
    B2F_MathContext* Context = nullptr;
    bool bRuntimeInitialized = false;
    RuntimeShutdownFn RuntimeShutdown = nullptr;
    DestroyFirmwareVehicleFn DestroyFirmwareVehicle = nullptr;
    StepFirmwareVehicleFn StepFirmwareVehicle = nullptr;
    FString Status = TEXT("Math backend not loaded");
};
