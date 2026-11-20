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
    const FName SunMirror             = FName(TEXT("SunMirror"));
}

namespace
{
    // De-harmonised Gerstner spectrum, kept in lock-step with the water
    // material's HLSL (see create_shiomori_map.py's water_material() custom
    // node). Sixteen components (two swell + fourteen wind/detail waves) with
    // de-harmonised lengths and a broad directional spread. This is the single
    // source for both the full 16-component sum (rendered look) and the
    // 6-component fast gameplay query.
    //
    //   component i:  length  L_i = SwellLength * LenF[i]
    //                 dir     D_i = PrimaryWaveDirection + DirOff[i]
    //                 amp     A_i = SwellAmplitude * AmpF[i] * SeaStateScale
    //                 omega   w_i = 2*pi * SwellSpeed / (SwellLength * sqrt(LenF[i]))
    //   height = sum_i A_i * cos(k_i * dot(P,D_i) - w_i * T + Phase[i])
    //            where k_i = 2*pi / L_i
    constexpr float LenF[16]    = { 1.0f, 0.5f, 0.33f, 0.22f, 0.151f, 0.107f, 0.079f, 0.059f, 0.045f, 0.034f, 0.0265f, 0.0205f, 0.016f, 0.0125f, 0.0098f, 0.0076f };
    constexpr float AmpF[16]    = { 1.00f, 0.42f, 0.30f, 0.21f, 0.15f, 0.105f, 0.075f, 0.054f, 0.038f, 0.027f, 0.019f, 0.0135f, 0.0095f, 0.0067f, 0.0047f, 0.0033f };
    constexpr float DirOff[16]  = { 0.0f, -8.0f, 12.0f, -20.0f, 28.0f, -35.0f, 45.0f, -55.0f, 65.0f, -50.0f, 40.0f, -30.0f, 50.0f, -60.0f, 22.0f, -15.0f };
    constexpr float Phase[16]   = { 0.0f, 1.7f, 3.1f, 4.4f, 5.7f, 1.1f, 2.4f, 3.7f, 5.0f, 0.6f, 1.9f, 3.2f, 4.5f, 5.8f, 0.9f, 2.2f };

    // Sum the first NumComponents of the Gerstner spectrum at world XY (cm) at
    // time T. Rebuilds the cached direction vectors only when the primary
    // direction changed since the last call — the surf mesh samples this 38k×
    // per tick, and re-deriving 16 DirectionVectors per vertex was ~1.8M trig
    // per tick wasted on direction alone.
    inline float GerstnerSum(const FOceanParameters& Params, const FVector2D& P, float Time, int32 NumComponents)
    {
        if (!Params.bDirCacheValid || Params.CachedDirDegrees != Params.PrimaryWaveDirection)
        {
            for (int32 i = 0; i < 16; ++i)
                Params.CachedDirVectors[i] = FOceanParameters::DirectionVector(Params.PrimaryWaveDirection + DirOff[i]);
            Params.CachedDirDegrees = Params.PrimaryWaveDirection;
            Params.bDirCacheValid = true;
        }

        const float AmpScale = Params.SeaStateScale();
        const float Wl = FMath::Max(1.0f, Params.SwellLength);
        const float Cs = FMath::Max(0.0f, Params.SwellSpeed);

        float H = 0.0f;
        for (int32 i = 0; i < NumComponents; ++i)
        {
            const float L = Wl * LenF[i];
            const float A = Params.SwellAmplitude * AmpF[i] * AmpScale;
            if (A <= 0.0f || L <= 0.0f)
                continue;
            const FVector2D& D = Params.CachedDirVectors[i];
            const float K = 6.28318530718f / L;
            const float Omega = 6.28318530718f * Cs / (Wl * FMath::Sqrt(LenF[i]));
            H += A * FMath::Cos(K * (P.X * D.X + P.Y * D.Y) - Omega * Time + Phase[i]);
        }
        return H;
    }
}

float FOceanParameters::SampleWaveHeight(const FVector2D& P, float Time) const
{
    return GerstnerSum(*this, P, Time, 16)
         * FMath::SmoothStep(100., 3000., P.Y - Born2FlapSurf::Shore(P.X));
}

float FOceanParameters::SampleWaveHeightFast(const FVector2D& P, float Time) const
{
    return GerstnerSum(*this, P, Time, 6)
         * FMath::SmoothStep(100., 3000., P.Y - Born2FlapSurf::Shore(P.X));
}