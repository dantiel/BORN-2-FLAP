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
    FMenuSettings()
    {
        // Defaults mirror ABorn2FlapFlightPawn's constructor (reference kestrel:
        // VOTIK PTK 7465 Plus servo, 3S 1.3 Ah, 30C/60C pack). Loading from
        // FlightPreferences.ini overrides these; they only matter when a key is
        // absent (fresh install), so the menu never shows zeroed tuning.
        Tuning.servo_no_load_speed_deg_s = 750;
        Tuning.servo_stall_torque_nm = 1.332;
        Tuning.servo_backdrive_deg_s_nm = 20;
        Tuning.battery_voltage = 11.1;
        Tuning.battery_resistance_ohm = 0.08;
        Tuning.battery_capacity_ah = 1.3;
        Tuning.battery_c_rating = 30;
        Tuning.battery_burst_c_rating = 60;
        Tuning.flap_base_freq_dhz = 32;
        Tuning.tail_elevator_angle_deg = 0;
        Tuning.glide_angle_deg = -4;
        Tuning.stroke_ferocity = 50;
        Tuning.aileron_scale = 10;
        Tuning.elevator_scale = 55;
        Tuning.flap_centre_deg = 0;
        Tuning.vertical_mount_angle_deg = 0;
    }
    // -1 = never explicitly chosen → the level's default bird applies.
    int32 BirdModel = -1;
    float BodyMassKg = 0.45f;
    float CgOffsetMm = 0.f;
    bool bCoupledThrottle = true;
    bool bFpvAirView = false;
    float FpvCameraAngleDeg = 0.f;
    bool bFreeLookInvert = false;
    // Mobile twin-stick layout (iOS/Android). ThrottleStick / RollStick select
    // which on-screen stick carries each channel (0 = left, 1 = right). Pitch is
    // always on the non-throttle stick; yaw is always on the non-roll stick.
    bool bMobileInvertPitch = false;
    int32 MobileThrottleStick = 0;   // 0 left · 1 right (Mode-2 default)
    int32 MobileRollStick = 1;       // 0 left · 1 right (Mode-2 default)
    FVector MouseGains = FVector(1.f, -1.f, 1.f);
    float ControlExpo = 0.65f;
    float SpeedModifier = 0.5f;
    bool bReplaySpirits = true;
    float FlightSafety = 1.f;
    float WingbeatVolume = 1.f;
    // UI language code ("en", "de", ...) — matches the locale JSON filenames.
    FString Language = TEXT("en");
    // Global UI scale (mobile "UI scale" setting). 1.0 = design-token default;
    // applied to fonts, spacing, element sizes and corner radius via theme::Scale.
    float UIScale = 1.0f;
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