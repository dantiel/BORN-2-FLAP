#include "Rendering/Born2FlapGraphics.h"
#include "HAL/IConsoleManager.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Paths.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "HAL/FileManager.h"

namespace
{
FString PreferencesPath()
{
    return FPaths::ProjectSavedDir() / (FParse::Param(FCommandLine::Get(), TEXT("B2FDesktopInputTest"))
        ? TEXT("Automation/FlightPreferences.ini") : TEXT("Config/FlightPreferences.ini"));
}

// Missing console variables are silently ignored: MegaLights is experimental
// and r.RayTracing may be absent on non-DXR platforms, so every toggle must
// degrade to a no-op rather than an assert.
void SetCVar(const TCHAR* Name, int32 Value)
{
    if (IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(Name))
    {
        Var->Set(Value, ECVF_SetByCode);
    }
}
}  // namespace

namespace Born2FlapGraphics
{

FSettings ForQuality(EQuality Q)
{
    FSettings S;
    switch (Q)
    {
    case EQuality::Medium:
        S.bVolumetricFog = true;
        S.bVolumetricClouds = true;
        break;
    case EQuality::High:
        S.bVolumetricFog = true;
        S.bVolumetricClouds = true;
        S.bHardwareRayTracing = true;
        S.bHardwareLumenReflections = true;
        break;
    case EQuality::Epic:
        S.bVolumetricFog = true;
        S.bVolumetricClouds = true;
        S.bHardwareRayTracing = true;
        S.bHardwareLumenReflections = true;
        S.bHardwareLumenGI = true;
        S.bMegaLights = true;
        break;
    case EQuality::Low:
    default:
        break;
    }
    return S;
}

bool SupportsNiagara()
{
    return LoadQuality() >= static_cast<int32>(EQuality::High);
}

void Apply(const FSettings& S)
{
    SetCVar(TEXT("r.VolumetricFog"), S.bVolumetricFog ? 1 : 0);
    SetCVar(TEXT("r.VolumetricCloud"), S.bVolumetricClouds ? 1 : 0);
    SetCVar(TEXT("r.RayTracing"), S.bHardwareRayTracing ? 1 : 0);
    SetCVar(TEXT("r.Lumen.Reflections.HardwareRayTracing"), S.bHardwareLumenReflections ? 1 : 0);
    SetCVar(TEXT("r.Lumen.HardwareRayTracing"), S.bHardwareLumenGI ? 1 : 0);
    SetCVar(TEXT("r.Lumen.ScreenProbeGather.HardwareRayTracing"), S.bHardwareLumenGI ? 1 : 0);
    SetCVar(TEXT("r.MegaLights"), S.bMegaLights ? 1 : 0);
}

int32 LoadQuality()
{
    const FString Path = PreferencesPath();
    FConfigFile Config;
    Config.Read(Path);
    // Default HIGH: hardware reflections + volumetric, but not the experimental
    // MegaLights or full HW GI — the best "many shaders" baseline without the
    // most aggressive paths. Players can raise to EPIC or drop to LOW.
    int32 Q = static_cast<int32>(EQuality::High);
    Config.GetInt(TEXT("Graphics"), TEXT("Quality"), Q);
    return FMath::Clamp(Q, 0, static_cast<int32>(EQuality::Count) - 1);
}

void SaveQuality(int32 Q)
{
    const FString Path = PreferencesPath();
    FConfigFile Config;
    Config.Read(Path);
    Config.bCanSaveAllSections = true;
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
    Config.SetInt64(TEXT("Graphics"), TEXT("Quality"),
        FMath::Clamp(Q, 0, static_cast<int32>(EQuality::Count) - 1));
    Config.Write(Path);
}

float LoadMotionBlur()
{
    const FString Path = PreferencesPath();
    FConfigFile Config;
    Config.Read(Path);
    float Amount = 0.5f;
    Config.GetFloat(TEXT("Graphics"), TEXT("MotionBlur"), Amount);
    return FMath::IsFinite(Amount) ? FMath::Clamp(Amount, 0.f, 1.f) : 0.5f;
}

void SaveMotionBlur(float Amount)
{
    const FString Path = PreferencesPath();
    FConfigFile Config;
    Config.Read(Path);
    Config.bCanSaveAllSections = true;
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
    Config.SetFloat(TEXT("Graphics"), TEXT("MotionBlur"),
        FMath::Clamp(Amount, 0.f, 1.f));
    Config.Write(Path);
}

void ApplyMotionBlur(float Amount)
{
    // 0 disables the whole pass (cheapest), otherwise full quality. The flight
    // pawn also scales its per-camera post-process amount by this value; the
    // global r.MotionBlur.Amount scalar is left at its engine default so the
    // per-camera override keeps its familiar 1:1 look.
    SetCVar(TEXT("r.MotionBlurQuality"), Amount <= 0.f ? 0 : 4);
}

bool LoadShowFps()
{
    const FString Path = PreferencesPath();
    FConfigFile Config;
    Config.Read(Path);
    bool bOn = true;
    Config.GetBool(TEXT("Graphics"), TEXT("ShowFps"), bOn);
    return bOn;
}

void SaveShowFps(bool bOn)
{
    const FString Path = PreferencesPath();
    FConfigFile Config;
    Config.Read(Path);
    Config.bCanSaveAllSections = true;
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
    Config.SetBool(TEXT("Graphics"), TEXT("ShowFps"), bOn);
    Config.Write(Path);
}

void ApplySavedOrDefault()
{
    Apply(ForQuality(static_cast<EQuality>(LoadQuality())));
}

const TCHAR* QualityName(EQuality Q)
{
    switch (Q)
    {
    case EQuality::Low:    return TEXT("LOW");
    case EQuality::Medium: return TEXT("MEDIUM");
    case EQuality::High:   return TEXT("HIGH");
    case EQuality::Epic:   return TEXT("EPIC");
    default:               return TEXT("LOW");
    }
}

}  // namespace Born2FlapGraphics