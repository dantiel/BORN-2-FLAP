// BORN 2 FLAP — Shiomori Bay coastal water system.
#include "Water/Born2FlapWater.h"
#include "Water/Born2FlapSurf.h"

namespace Born2FlapWater
{
    const FName OceanLevel            = FName(TEXT("OceanLevel"));
    const FName PrimaryWaveDirection  = FName(TEXT("PrimaryWaveDirection"));
    const FName WindDirection         = FName(TEXT("WindDirection"));
    const FName WindSpeed             = FName(TEXT("WindSpeed"));
    const FName SwellAmplitude        = FName(TEXT("SwellAmplitude"));
    const FName SwellLength           = FName(TEXT("SwellLength"));
    const FName SwellSpeed            = FName(TEXT("SwellSpeed"));
    const FName SeaState              = FName(TEXT("SeaState"));
    const FName FoamAmount            = FName(TEXT("FoamAmount"));
    const FName StormAmount           = FName(TEXT("StormAmount"));
    const FName ShoreBreakIntensity   = FName(TEXT("ShoreBreakIntensity"));
    const FName WaveTime              = FName(TEXT("WaveTime"));
    const FName QualityLevel          = FName(TEXT("QualityLevel"));
    const FName WaterLOD              = FName(TEXT("WaterLOD"));
}

float FOceanParameters::SampleWaveHeight(const FVector2D& P, float Time) const
{
    // Deterministic, cheap Gerstner spectrum (8 components). Must match the
    // HLSL in create_shiomori_map.py's water_material() custom node exactly so
    // gameplay water height and the rendered surface agree.
    //
    //   component i:  length  L_i = SwellLength * lenF[i]
    //                 dir     D_i = PrimaryWaveDirection + dirOff[i]
    //                 amp     A_i = SwellAmplitude * ampF[i] * SeaStateScale
    //                 omega   w_i = 2*pi * SwellSpeed / (SwellLength * sqrt(lenF[i]))
    //   height = sum_i A_i * cos(k_i * dot(P,D_i) - w_i * T + phase[i])
    //            where k_i = 2*pi / L_i
    //
    static constexpr float LenF[8]    = { 1.0f, 0.32f, 0.19f, 0.13f, 0.088f, 0.059f, 0.043f, 0.030f };
    static constexpr float AmpF[8]    = { 1.00f, 0.45f, 0.28f, 0.16f, 0.090f, 0.055f, 0.035f, 0.022f };
    static constexpr float DirOff[8]  = { 0.0f, -8.0f, 7.0f, -12.0f, 18.0f, -22.0f, 28.0f, -35.0f };
    static constexpr float Phase[8]   = { 0.0f, 1.3f, 2.7f, 4.1f, 5.2f, 0.9f, 3.3f, 4.7f };

    const float AmpScale = SeaStateScale();
    const float Wl = FMath::Max(1.0f, SwellLength);
    const float Cs = FMath::Max(0.0f, SwellSpeed);

    float H = 0.0f;
    for (int32 i = 0; i < 8; ++i)
    {
        const float L = Wl * LenF[i];
        const float A = SwellAmplitude * AmpF[i] * AmpScale;
        if (A <= 0.0f || L <= 0.0f)
            continue;
        const FVector2D D = DirectionVector(PrimaryWaveDirection + DirOff[i]);
        const float K = 6.28318530718f / L;
        const float Omega = 6.28318530718f * Cs / (Wl * FMath::Sqrt(LenF[i]));
        H += A * FMath::Cos(K * (P.X * D.X + P.Y * D.Y) - Omega * Time + Phase[i]);
    }
    return H*FMath::SmoothStep(100.,3000.,P.Y-Born2FlapSurf::Shore(P.X));
}
