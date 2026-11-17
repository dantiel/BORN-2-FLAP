#pragma once
// Born2FlapServo.h — the servo-desk backend (single source of truth).
//
// The servo catalog (OrniFlight-tested presets), the PTK 7465 Plus power-
// degradation shape and the preset-apply helper feed the Brain-authored
// UMGHAML tuning editor (published via ABorn2FlapGameMode::BuildBrainTelemetry).
// Layout lives in the Brain (Brain/lib/born2flap/ui/views/tuning_editor);
// this header only supplies data + the apply operation.

#include "CoreMinimal.h"
#include "Math/Born2FlapMathBridge.h"

namespace born2flap::servo {

// Servos tested for flapping (dantiel.github.io/OrniFlight servo-efficiency
// table). Speed = 60 / s-per-60°; torque = kg·cm × 0.0980665 → N·m, then × 1.8
// servo-power fidelity — the solver under-models a servo's effective authority,
// so stall torque carries the ×1.8 to match measured flap authority. Backdrive
// is not published on the table, so presets keep the simulator default (20).
// A preset stores its nominal-voltage row (PTK 7465 Plus: 857°/s @ 7.4 V); the
// full voltage curve incl. 8.4 V (983.6°/s) lives in MathCore Servo.hs.
struct FServoPreset
{
    const TCHAR* Name;
    float SpeedDegS;      // no-load speed at the rated (nominal) voltage
    float StallTorqueNm;
    float Backdrive;
    float Voltage;        // rated (nominal) voltage
    float MinVoltage;     // operating range
    float MaxVoltage;
};

static const FServoPreset Presets[] = {
    { TEXT("Blue Arrow AF D43S-6.0-MG"), 1500.f, 0.31f, 20.f, 6.0f, 4.8f, 6.0f },
    { TEXT("VOTIK PTK 7465 / 7465W MG"),   667.f, 1.01f, 20.f, 8.4f, 6.0f, 8.4f },
    { TEXT("VOTIK PTK 7465 Plus"),         857.f, 1.32f, 20.f, 8.4f, 5.0f, 8.4f },
    { TEXT("Blue Arrow D0576HT-MG-HV"),   1000.f, 0.74f, 20.f, 7.4f, 6.0f, 8.4f },
    { TEXT("Inservos D0474HT-MG-HV"),      750.f, 0.63f, 20.f, 7.4f, 6.0f, 8.4f },
    { TEXT("KST MR320"),                   750.f, 0.97f, 20.f, 7.4f, 6.0f, 8.4f },
    { TEXT("SAVOX SV-1270TG"),             545.f, 6.17f, 20.f, 7.4f, 6.0f, 7.4f },
};
constexpr int32 PresetCount = (int32)(sizeof(Presets) / sizeof(Presets[0]));

// PTK 7465 Plus datasheet power shape (Servo.hs rows): speed × torque at
// 5 / 6 / 7.4 / 8.4 V, normalised to the 8.4 V row. Power degrades steeply
// below 7.4 V — this shape feeds the degradation curve on the servo desk.
FORCEINLINE float PowerFraction(float Voltage)
{
    static const float V[] = { 5.0f, 6.0f, 7.4f, 8.4f };
    static const float P[] = { 0.2476f, 0.3876f, 0.7028f, 1.0000f };
    if (Voltage <= V[0]) return P[0];
    if (Voltage >= V[3]) return P[3];
    for (int32 I = 0; I < 3; ++I)
        if (Voltage >= V[I] && Voltage <= V[I + 1])
        {
            const float F = (Voltage - V[I]) / (V[I + 1] - V[I]);
            return P[I] + F * (P[I + 1] - P[I]);
        }
    return 1.0f;
}

// Apply a preset's tested defaults to a tuning config (speed, stall torque,
// backdrive and the pack voltage the servo is rated for).
FORCEINLINE void ApplyPreset(B2F_TuningConfig& T, int32 Idx)
{
    const FServoPreset& P = Presets[FMath::Clamp(Idx, 0, PresetCount - 1)];
    T.servo_no_load_speed_deg_s = P.SpeedDegS;
    T.servo_stall_torque_nm = P.StallTorqueNm;
    T.servo_backdrive_deg_s_nm = P.Backdrive;
    T.battery_voltage = P.Voltage;
}

// Power-degradation series across the preset's operating range: N samples
// from max(MinVoltage, 4.8) to MaxVoltage, normalised to the rated-voltage
// row (rated = 1.0). Feeds the Sparkline widget on the servo desk.
FORCEINLINE void DegradationCurve(const FServoPreset& P, TArray<float>& Out, int32 N = 13)
{
    Out.Reset(FMath::Max(2, N));
    const float V0 = FMath::Max(P.MinVoltage, 4.8f);
    const float PAt = FMath::Max(1e-3f, PowerFraction(P.Voltage));
    for (int32 I = 0; I < N; ++I)
    {
        const float V = V0 + (P.MaxVoltage - V0) * float(I) / float(N - 1);
        Out.Add(PowerFraction(V) / PAt);
    }
}

}  // namespace born2flap::servo