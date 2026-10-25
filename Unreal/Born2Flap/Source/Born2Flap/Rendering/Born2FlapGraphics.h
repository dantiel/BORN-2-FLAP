#pragma once
// Born2FlapGraphics.h — renderer quality tiers + feature toggles.
//
// The renderer ships with Unreal's full shader stack (Lumen, Virtual Shadow
// Maps, TSR, Nanite, Substrate). What varies is how much of that stack runs on
// dedicated hardware vs. the software fallback paths. These tiers map onto
// console variables only — turning a feature off is exactly the "software
// method": Lumen falls back to screen-space + signed-distance-field tracing,
// reflections fall back to screen-space, volumetric fog falls back to plain
// exponential height fog, and so on. No extra software implementation lives
// here; the engine's non-HW path IS the fallback.

#include "CoreMinimal.h"

namespace Born2FlapGraphics
{
    // LOW is the long-shipping software baseline; each tier up enables more
    // hardware/shader features. Ordering matters for the settings menu.
    enum class EQuality : int32
    {
        Low = 0,     // software Lumen + screen-space reflections, no volumetric fog/clouds, no HW RT
        Medium = 1,  // + volumetric fog + volumetric clouds
        High = 2,    // + hardware ray tracing + HW Lumen reflections
        Epic = 3,    // + full hardware Lumen GI + MegaLights
        Count = 4
    };

    // Per-feature flags, all defaulting to the LOW (software) state. A tier
    // populates these; advanced toggles can override before Apply().
    struct FSettings
    {
        bool bVolumetricFog = false;
        bool bVolumetricClouds = false;
        bool bHardwareRayTracing = false;       // master r.RayTracing
        bool bHardwareLumenReflections = false; // r.Lumen.Reflections.HardwareRayTracing
        bool bHardwareLumenGI = false;          // r.Lumen.HardwareRayTracing (+ screen-probe gather)
        bool bMegaLights = false;               // r.MegaLights (experimental)
    };

    BORN2FLAP_API FSettings ForQuality(EQuality Q);
    BORN2FLAP_API void Apply(const FSettings& S);
    // Niagara particle effects (rain/snow/insect swarm) run on the hardware
    // path only (HIGH/EPIC); LOW/MEDIUM keep the instanced-mesh software
    // fallback. Mirrors the rest of the tier ladder.
    BORN2FLAP_API bool SupportsNiagara();
    // Read/save the tier to Config/FlightPreferences.ini [Graphics] Quality.
    BORN2FLAP_API int32 LoadQuality();
    BORN2FLAP_API void SaveQuality(int32 Q);
    // Load the saved tier and apply it (startup hook).
    BORN2FLAP_API void ApplySavedOrDefault();
    BORN2FLAP_API const TCHAR* QualityName(EQuality Q);

    // Motion blur: a user preference independent of the hardware tier. Stored
    // in [Graphics] MotionBlur as a 0..1 amount; the flight pawn scales its
    // per-camera strength by it (0 = fully disabled).
    BORN2FLAP_API float LoadMotionBlur();
    BORN2FLAP_API void  SaveMotionBlur(float Amount);
    // Reflect the setting in the global motion-blur pass immediately (on/off at
    // the console level) so the menu applies it without a re-spawn.
    BORN2FLAP_API void  ApplyMotionBlur(float Amount);

    // FPS counter: a HUD overlay toggle, persisted in [Graphics] ShowFps.
    // Defaults to ON so the frame-rate readout is visible out of the box.
    BORN2FLAP_API bool LoadShowFps();
    BORN2FLAP_API void SaveShowFps(bool bOn);
}