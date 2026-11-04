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
    case ETuningField::BatteryCRating:     return TEXT("BatteryCRating");
    case ETuningField::BatteryBurstCRating: return TEXT("BatteryBurstCRating");
    case ETuningField::FlapBaseFreq:       return TEXT("FlapBaseFreq");
    case ETuningField::TailElevatorAngle:       return TEXT("TailElevatorAngle");
    case ETuningField::GlideAngle:         return TEXT("GlideAngle");
    case ETuningField::StrokeFerocity:     return TEXT("StrokeFerocity");
    case ETuningField::AileronScale:       return TEXT("AileronScale");
    case ETuningField::ElevatorScale:      return TEXT("ElevatorScale");
    case ETuningField::FlapCentre:         return TEXT("FlapCentre");
    case ETuningField::VerticalMountAngle: return TEXT("VerticalMountAngle");
    case ETuningField::ReturnFerocity:    return TEXT("ReturnFerocity");
    case ETuningField::YawScale:          return TEXT("YawScale");
    case ETuningField::StrokeSkew:        return TEXT("StrokeSkew");
    case ETuningField::ReturnSkew:        return TEXT("ReturnSkew");
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
    { ETuningField::BatteryCRating,     TEXT("BATTERY C RATING"),      TEXT("C"),              5.f,  120.f, 0, TEXT("Continuous discharge rating. Low = sags under load; high = holds voltage through a hard downstroke.") },
    { ETuningField::BatteryBurstCRating,TEXT("BATTERY BURST C"),       TEXT("C"),              5.f,  240.f, 0, TEXT("Momentary burst rating (e.g. 60C = double continuous). A higher burst lets the LiPo punch harder for a split second, recharging between strokes.") },
    { ETuningField::FlapBaseFreq,       TEXT("FLAP FREQ CEILING"),     TEXT("dHz"),          10.f,  200.f, 0, TEXT("Ceiling of the flapping frequency. Low = slow, lazy wingbeat; high = fast, humming stroke (needs a fast servo).") },
    { ETuningField::TailElevatorAngle,     TEXT("TAIL ELEVATOR ANGLE"),      TEXT("°"),           -15.f,   15.f, 1, TEXT("Physical tail mounting angle. Negative incidence usually produces tail downforce and a nose-up moment in forward flight. The Kestrel tail remains fixed to the fuselage at this angle. Flap Centre adjusts the wings separately.") },
    { ETuningField::GlideAngle,         TEXT("GLIDE ANGLE"),           TEXT("°"),           -15.f,   15.f, 0, TEXT("Glide incidence trim. Negative = steeper nose-down glide; positive = shallower with more float.") },
    { ETuningField::StrokeFerocity,     TEXT("STROKE FEROCITY"),       TEXT("%"),             0.f,  100.f, 0, TEXT("How aggressively the stroke accelerates through centre. Low = soft/round; high = crisp snap; very high = violent.") },
    { ETuningField::AileronScale,       TEXT("AILERON SCALE"),         TEXT("%"),             0.f,  100.f, 0, TEXT("Roll authority. Low = gentle banking; high = very responsive roll.") },
    { ETuningField::ElevatorScale,      TEXT("ELEVATOR SCALE"),        TEXT("%"),             0.f,  100.f, 0, TEXT("Pitch authority from the ruddervators. Low = subtle pitch; high = very strong pitch response.") },
    { ETuningField::FlapCentre,         TEXT("FLAP CENTRE"),          TEXT("°"),           -15.f,   15.f, 0, TEXT("Powered stroke-centre trim. Negative raises both wings (dihedral); positive lowers them (anhedral). Changes sideslip stability through actual wing geometry. It is not wing incidence.") },
    { ETuningField::VerticalMountAngle, TEXT("VERTICAL MOUNT ANGLE"), TEXT("°"),           -15.f,   15.f, 0, TEXT("Real wing mount incidence (nose-up +). Tilts the wing about its span axis; feeds the strip's geometric twist. Negative = nose-down; positive = nose-up.") },
    { ETuningField::ReturnFerocity,    TEXT("RETURN FEROCITY"),      TEXT("%"),             0.f,  100.f, 0, TEXT("How aggressively the upstroke/return accelerates through centre, independently of the downstroke. Low = soft return; high = crisp snap.") },
    { ETuningField::YawScale,          TEXT("YAW SCALE"),            TEXT("%"),             0.f,  100.f, 0, TEXT("Maximum differential wing amplitude in a yaw turn. Low = symmetric stroke; high = one wing beats far deeper than the other for a strong visual + proverse-yaw swing.") },
    { ETuningField::StrokeSkew,        TEXT("STROKE SKEW"),          TEXT("%"),          -100.f,  100.f, 0, TEXT("Downstroke centre-skew. Positive front-loads thrust (peak earlier); negative delays it. Mirrors PteronautOS strokeSkewPercent.") },
    { ETuningField::ReturnSkew,        TEXT("RETURN SKEW"),          TEXT("%"),          -100.f,  100.f, 0, TEXT("Upstroke centre-skew. Positive front-loads the return; negative delays it. Mirrors PteronautOS returnSkewPercent.") },
};

FORCEINLINE const FRow& Row(ETuningField Field)
{
    for (const FRow& R : Rows)
        if (R.Field == Field)
            return R;
    return Rows[0];
}

// ── Per-model factory defaults ────────────────────────────────────────────
// Each silhouette has its own reset target for the whole tuning profile (servo,
// battery, geometry, ferocity, authority, waveform) plus body mass + CG. These
// are the values a per-model profile falls back to before any user override is
// stored under [Tuning.Model<N>] in FlightPreferences.ini.

FORCEINLINE B2F_TuningConfig DefaultTuningForModel(int32 Model)
{
    B2F_TuningConfig T{};
    // Shared reference defaults (VOTIK PTK 7465 servo, 3S 1.3 Ah 30C/60C pack).
    T.servo_no_load_speed_deg_s = 750;
    T.servo_stall_torque_nm = 1.332;
    T.servo_backdrive_deg_s_nm = 20;
    T.battery_voltage = 11.1;
    T.battery_resistance_ohm = 0.08;
    T.battery_capacity_ah = 1.3;
    T.battery_c_rating = 30;
    T.battery_burst_c_rating = 60;
    T.flap_base_freq_dhz = 32;
    T.tail_elevator_angle_deg = -2.3;
    T.glide_angle_deg = -4;
    T.stroke_ferocity = 50;
    T.aileron_scale = 40;
    T.elevator_scale = 55;
    T.flap_centre_deg = 0;
    T.body_mass_kg = 0.050;
    T.vertical_mount_angle_deg = 0;
    T.return_ferocity = 50;
    T.yaw_scale = 60;
    T.stroke_skew_percent = 0;
    T.return_skew_percent = 0;

    switch (Model)
    {
    case 0:  // RAVENCROW — big night hunter: stronger servo, heavier, more pitch authority.
        T.servo_stall_torque_nm = 1.8;
        T.elevator_scale = 65;
        T.body_mass_kg = 0.086;
        break;
    case 1:  // PROTOTYPE — training workhorse: gentle stroke, light.
        T.stroke_ferocity = 45;
        T.body_mass_kg = 0.036;
        break;
    case 2:  // COMMON KESTREL — reference bird.
        break;
    case 3:  // NEON TURTLE — weightless aether swimmer.
        break;
    case 4:  // KESTREL LIGHT — halved rods + weight: fast micro servo.
        T.servo_no_load_speed_deg_s = 1200;
        T.servo_stall_torque_nm = 0.5;
        T.body_mass_kg = 0.025;
        break;
    case 5:  // PTEROSAUR — kestrel physics, desert glider skin.
        break;
    default:
        break;
    }
    return T;
}

FORCEINLINE float DefaultBodyMassForModel(int32 Model)
{
    return float(DefaultTuningForModel(Model).body_mass_kg);
}

FORCEINLINE float DefaultCgOffsetForModel(int32 Model)
{
    (void)Model;
    return 0.f;   // neutral CG for every silhouette by default
}

}  // namespace born2flap::tuning