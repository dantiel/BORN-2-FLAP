#pragma once
// Born2FlapMenuSettings.h — a pawn-independent mirror of the flight/tuning
// preferences the full-screen menu edits.
//
// The FLIGHT DESK / GENERAL SETTINGS / CONTROL SETTINGS pages used to be
// reachable only from inside a level, because they read and wrote a live
// ABorn2FlapFlightPawn. This store lets the menu own the same state without a
// pawn: it persists to the SAME Config/FlightPreferences.ini the pawn loads at
// spawn, so edits made on the home screen are picked up when a world is entered,
// and edits made in-flight are still applied live (the menu keeps both in sync).

#include "CoreMinimal.h"
#include "Math/Born2FlapMathBridge.h"

class ABorn2FlapFlightPawn;
enum class ETuningField : uint8;

namespace born2flap {

struct FMenuSettings
{
    B2F_TuningConfig Tuning{};
    // -1 = never explicitly chosen → the level's default bird applies.
    int32 BirdModel = -1;
    float BodyMassKg = 0.45f;
    float CgOffsetMm = 0.f;
    bool bCoupledThrottle = true;
    bool bFpvAirView = false;
    float FpvCameraAngleDeg = 0.f;
    bool bFreeLookInvert = false;
    FVector MouseGains = FVector(1.f, -1.f, 1.f);
    float ControlExpo = 0.65f;
    float SpeedModifier = 0.5f;
    bool bReplaySpirits = true;
    float FlightSafety = 1.f;
    float WingbeatVolume = 1.f;
    // UI language code ("en", "de", ...) — matches the locale JSON filenames.
    FString Language = TEXT("en");
};

// Load/save the store from Config/FlightPreferences.ini (same keys as the pawn).
void MenuSettingsLoad(FMenuSettings& Out);
void MenuSettingsSave(const FMenuSettings& In);

// Copy a live pawn's state into the store (menu opened in-level).
void MenuSettingsFromPawn(const ABorn2FlapFlightPawn* Pawn, FMenuSettings& Out);
// Push the store's state into a live pawn (applies live flight/tuning changes).
void MenuSettingsToPawn(const FMenuSettings& In, ABorn2FlapFlightPawn* Pawn);

// Tuning-field access against the store's B2F_TuningConfig (mirrors the pawn's
// GetTuning/SetTuning/TuningClamp). Values stay in SI units.
float MenuSettingsGetTuning(const FMenuSettings& S, ETuningField Field);
void  MenuSettingsSetTuning(FMenuSettings& S, ETuningField Field, float Value);
float MenuSettingsTuningClamp(ETuningField Field, float Value);

}  // namespace born2flap