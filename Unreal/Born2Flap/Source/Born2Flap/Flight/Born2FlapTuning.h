#pragma once
// Born2FlapTuning.h — shared tuning metadata for the live-tuning surface.
//
// The hangar edits twelve firmware knobs (servo, battery, controller scales).
// This header is the single source of truth for:
//   * the config-key string each field persists under (Key),
//   * the editor row metadata (label / unit / min / max / decimals).
//
// Consumed by both the preferences persistence (Flight/Born2FlapFlightSettings.cpp)
// and the semantic settings panel (UI/Born2FlapFlightSettings.cpp) so the two can
// never drift apart.

#include "Flight/Born2FlapFlightPawn.h"

namespace born2flap::tuning {

FORCEINLINE const TCHAR* Key(ETuningField Field)
{
    switch (Field)
    {
    case ETuningField::ServoSpeed:         return TEXT("ServoSpeed");
    case ETuningField::StallTorque:        return TEXT("StallTorque");
    case ETuningField::Backdrive:          return TEXT("Backdrive");
    case ETuningField::BatteryVoltage:     return TEXT("BatteryVoltage");
    case ETuningField::BatteryResistance:  return TEXT("BatteryResistance");
    case ETuningField::BatteryCapacity:    return TEXT("BatteryCapacity");
    case ETuningField::FlapBaseFreq:       return TEXT("FlapBaseFreq");
    case ETuningField::TailElevatorAngle:       return TEXT("TailElevatorAngle");
    case ETuningField::GlideAngle:         return TEXT("GlideAngle");
    case ETuningField::StrokeFerocity:     return TEXT("StrokeFerocity");
    case ETuningField::AileronScale:       return TEXT("AileronScale");
    case ETuningField::ElevatorScale:      return TEXT("ElevatorScale");
    case ETuningField::MountAngle:         return TEXT("MountAngle");
    default:                               return TEXT("");
    }
}

FORCEINLINE ETuningField FromKey(const FString& KeyString)
{
    for (uint8 I = 0; I < (uint8)ETuningField::Count; ++I)
    {
        const ETuningField Field = (ETuningField)I;
        if (KeyString == Key(Field))
            return Field;
    }
    return ETuningField::Count;
}

struct FRow
{
    ETuningField Field;
    const TCHAR* Label;
    const TCHAR* Unit;
    float Min, Max;
    int32 Decimals;
    const TCHAR* Help;
};

// Display-unit conversion. The physics/tuning state stays in SI units (servo
// no-load speed in °/s, stall torque in N·m); the editor exposes the hobbyist
// units pilots actually spec servos with:
//   * speed  -> seconds per 60° (reciprocal of °/s)
//   * torque -> kg·cm  (1 kg·cm = 0.0980665 N·m)
FORCEINLINE float ToDisplayValue(ETuningField Field, float Internal)
{
    switch (Field)
    {
    case ETuningField::ServoSpeed:  return Internal > 1e-3f ? 60.f / Internal : 0.f;
    case ETuningField::StallTorque: return Internal / 0.0980665f;
    default:                        return Internal;
    }
}
FORCEINLINE float FromDisplayValue(ETuningField Field, float Display)
{
    switch (Field)
    {
    case ETuningField::ServoSpeed:  return Display > 1e-3f ? 60.f / Display : 0.f;
    case ETuningField::StallTorque: return Display * 0.0980665f;
    default:                        return Display;
    }
}

static const FRow Rows[] =
{
    { ETuningField::ServoSpeed,         TEXT("SERVO SPEED"),           TEXT("s/60°"),       0.025f,  0.6f, 3, TEXT("Seconds per 60° stroke. Low = faster/more agile; high = slower and gentler on the servo.") },
    { ETuningField::StallTorque,        TEXT("STALL TORQUE"),          TEXT("kg·cm"),         0.5f,  40.f, 1, TEXT("How hard the servo can push before stalling. Low = weak micro servo; high = very strong metal-gear. Too low and the wing stalls mid-stroke.") },
    { ETuningField::Backdrive,          TEXT("BACKDRIVE"),             TEXT("°/s per N·m"),   0.f,  100.f, 0, TEXT("How easily the wing pushes the servo backwards when aero load exceeds torque. Low = stiff/holds shape; high = soft/backdrivable.") },
    { ETuningField::BatteryVoltage,     TEXT("BATTERY VOLTAGE"),       TEXT("V"),             3.7f,  22.2f, 1, TEXT("Pack voltage. Low (1S) = light but weak; high (6S) = heavy with far more servo headroom and speed.") },
    { ETuningField::BatteryResistance,  TEXT("BATTERY RESISTANCE"),    TEXT("Ω"),             0.01f,  0.5f, 2, TEXT("Pack internal resistance. Low = punchy, holds voltage; high = sags under load and the servo slows.") },
    { ETuningField::BatteryCapacity,    TEXT("BATTERY CAPACITY"),      TEXT("Ah"),            0.1f,   5.f, 2, TEXT("Pack capacity. High = long flight but heavy; low = short, light sprints.") },
    { ETuningField::FlapBaseFreq,       TEXT("FLAP FREQ CEILING"),     TEXT("dHz"),          10.f,  200.f, 0, TEXT("Ceiling of the flapping frequency. Low = slow, lazy wingbeat; high = fast, humming stroke (needs a fast servo).") },
    { ETuningField::TailElevatorAngle,     TEXT("TAIL ELEVATOR ANGLE"),      TEXT("°"),           -15.f,   15.f, 0, TEXT("Static tail-elevator trim shifting the flap-stroke centre. Negative = nose-down; positive = nose-up. Fine-tune level flight.") },
    { ETuningField::GlideAngle,         TEXT("GLIDE ANGLE"),           TEXT("°"),           -15.f,   15.f, 0, TEXT("Glide incidence trim. Negative = steeper nose-down glide; positive = shallower with more float.") },
    { ETuningField::StrokeFerocity,     TEXT("STROKE FEROCITY"),       TEXT("%"),             0.f,  100.f, 0, TEXT("How aggressively the stroke accelerates through centre. Low = soft/round; high = crisp snap; very high = violent.") },
    { ETuningField::AileronScale,       TEXT("AILERON SCALE"),         TEXT("%"),             0.f,  100.f, 0, TEXT("Roll authority. Low = gentle banking; high = very responsive roll.") },
    { ETuningField::ElevatorScale,      TEXT("ELEVATOR SCALE"),        TEXT("%"),             0.f,  100.f, 0, TEXT("Pitch authority from the ruddervators. Low = subtle pitch; high = very strong pitch response.") },
    { ETuningField::MountAngle,         TEXT("VERTICAL MOUNT ANGLE"), TEXT("°"),           -15.f,   15.f, 0, TEXT("Wing mount incidence added to the wing's angle of attack. Negative = nose-down bias; positive = nose-up bias.") },
};

FORCEINLINE const FRow& Row(ETuningField Field)
{
    for (const FRow& R : Rows)
        if (R.Field == Field)
            return R;
    return Rows[0];
}

}  // namespace born2flap::tuning