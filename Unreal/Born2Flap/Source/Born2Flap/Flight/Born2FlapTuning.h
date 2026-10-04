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
};

static const FRow Rows[] =
{
    { ETuningField::ServoSpeed,         TEXT("SERVO SPEED"),           TEXT("°/s"),         100.f, 2400.f, 0 },
    { ETuningField::StallTorque,        TEXT("STALL TORQUE"),          TEXT("N·m"),           0.5f,   20.f, 1 },
    { ETuningField::Backdrive,          TEXT("BACKDRIVE"),             TEXT("°/s per N·m"),   0.f,  100.f, 0 },
    { ETuningField::BatteryVoltage,     TEXT("BATTERY VOLTAGE"),       TEXT("V"),             3.7f,  22.2f, 1 },
    { ETuningField::BatteryResistance,  TEXT("BATTERY RESISTANCE"),    TEXT("Ω"),             0.01f,  0.5f, 2 },
    { ETuningField::BatteryCapacity,    TEXT("BATTERY CAPACITY"),      TEXT("Ah"),            0.1f,   5.f, 2 },
    { ETuningField::FlapBaseFreq,       TEXT("FLAP FREQ CEILING"),     TEXT("dHz"),          10.f,  200.f, 0 },
    { ETuningField::TailElevatorAngle,     TEXT("TAIL ELEVATOR ANGLE"),      TEXT("°"),           -15.f,   15.f, 0 },
    { ETuningField::GlideAngle,         TEXT("GLIDE ANGLE"),           TEXT("°"),           -15.f,   15.f, 0 },
    { ETuningField::StrokeFerocity,     TEXT("STROKE FEROCITY"),       TEXT("%"),             0.f,  100.f, 0 },
    { ETuningField::AileronScale,       TEXT("AILERON SCALE"),         TEXT("%"),             0.f,  100.f, 0 },
    { ETuningField::ElevatorScale,      TEXT("ELEVATOR SCALE"),        TEXT("%"),             0.f,  100.f, 0 },
};

}  // namespace born2flap::tuning