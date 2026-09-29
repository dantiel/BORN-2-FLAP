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

    // Inject the environmental wind phase noise η [rad/s] the MathCore resonance
    // layer is designed to counter. Takes effect next step; no-op if unready.
    bool InjectWindPhaseNoise(double NoiseRadS) const;
    bool ReadWingShape(B2F_WingSection* Left, B2F_WingSection* Right) const;

    const FString& GetStatus() const { return Status; }

private:
    using AbiVersionFn = uint32_t (*)();
    using RuntimeInitFn = int32_t (*)();
    using RuntimeShutdownFn = void (*)();
    using CreateFirmwareVehicleFn = B2F_MathContext* (*)(const B2F_FirmwareConfig*);
    using DestroyFirmwareVehicleFn = void (*)(B2F_MathContext*);
    using StepFirmwareVehicleFn = int32_t (*)(
        B2F_MathContext*, const B2F_PilotInput*, const B2F_BodyState*, B2F_FirmwareOutput*);
    using SetWindPhaseNoiseFn = int32_t (*)(B2F_MathContext*, double);
    using GetWingShapeFn = int32_t (*)(B2F_MathContext*, uint32_t, B2F_WingSection*, B2F_WingSection*);

    void* LibraryHandle = nullptr;
    B2F_MathContext* Context = nullptr;
    bool bRuntimeInitialized = false;
    RuntimeShutdownFn RuntimeShutdown = nullptr;
    DestroyFirmwareVehicleFn DestroyFirmwareVehicle = nullptr;
    StepFirmwareVehicleFn StepFirmwareVehicle = nullptr;
    SetWindPhaseNoiseFn SetWindPhaseNoise = nullptr;
    GetWingShapeFn GetWingShape = nullptr;
    FString Status = TEXT("Math backend not loaded");
};
