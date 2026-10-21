#include "World/Born2FlapWind.h"

namespace Born2FlapWind
{
namespace
{
constexpr int32 ThermalCount = 48;

// Deterministic 0..1 hash so the field is stable across runs and sessions.
double Hash01(int32 I, int32 Salt)
{
    uint32 H = uint32(I) * 2654435761u ^ uint32(Salt) * 40503u;
    H ^= H >> 13;
    H *= 0x5bd1e995u;
    H ^= H >> 15;
    return double(H) / double(0xFFFFFFFFu);
}

struct FThermal
{
    double X, Y;       // metres, drift origin
    double DriftX, DriftY; // m/s slow drift
    double R;          // metres radius
    double Strength;   // m/s peak updraft
};

FThermal Thermal(int32 I)
{
    FThermal T;
    T.X = (Hash01(I, 0) * 2.0 - 1.0) * 320.0;
    T.Y = (Hash01(I, 1) * 2.0 - 1.0) * 380.0;
    T.DriftX = (Hash01(I, 2) - 0.5) * 2.0;
    T.DriftY = (Hash01(I, 3) - 0.5) * 2.0;
    T.R = 18.0 + Hash01(I, 4) * 30.0;
    T.Strength = 3.0 + Hash01(I, 5) * 5.0;
    return T;
}
}

FVector Sample(const FVector& P, double Time)
{
    // Metres internally.
    const double X = P.X / 100.0, Y = P.Y / 100.0, Z = P.Z / 100.0;

    // Base wind: slowly rotating direction + breathing speed (metres/second).
    const double Dir = 0.35 * Time + 2.5 * FMath::PerlinNoise1D((float)(Time * 0.02));
    const double BaseSpeed = 2.5 + 1.5 * FMath::PerlinNoise1D((float)(Time * 0.015 + 40.0));
    const FVector Base(BaseSpeed * FMath::Cos(Dir), BaseSpeed * FMath::Sin(Dir), 0.0);

    // Gust turbulence: three decorrelated 3D noise channels (each -1..1).
    const double GX = FMath::PerlinNoise3D(FVector((float)(X * 0.05), (float)(Y * 0.05), (float)(Time * 0.30)));
    const double GY = FMath::PerlinNoise3D(FVector((float)(X * 0.05 + 100.0), (float)(Y * 0.05 + 100.0), (float)(Time * 0.30 + 50.0)));
    const double GZ = FMath::PerlinNoise3D(FVector((float)(X * 0.05 + 200.0), (float)(Y * 0.05 + 200.0), (float)(Time * 0.30 + 100.0)));
    const FVector Gust(GX, GY, GZ);

    // Thermals: drifting columns of buoyant updraft, peaking near 80 m.
    double Updraft = 0.0;
    for (int32 I = 0; I < ThermalCount; ++I)
    {
        const FThermal T = Thermal(I);
        const double Tx = T.X + T.DriftX * Time;
        const double Ty = T.Y + T.DriftY * Time;
        const double Dx = X - Tx, Dy = Y - Ty;
        const double Radial = FMath::Exp(-(Dx * Dx + Dy * Dy) / (T.R * T.R));
        // Vertical profile: zero at the ground, peaks at 80 m, decays above.
        const double Zp = Z <= 0.0 ? 0.0 : (Z / 80.0) * FMath::Exp(1.0 - Z / 80.0);
        Updraft += T.Strength * Radial * Zp;
    }

    // Terrain/building/tree windward lift is layered in elsewhere
    // (Born2FlapWeather::Wind via WindwardLift + the level's height sampler)
    // because this field has no terrain access. Here it supplies base wind,
    // turbulence and thermals only.
    FVector Wind = Base + Gust * 3.0;
    Wind.Z = Updraft + Gust.Z * 0.6;
    return Wind;
}

double PhaseNoise(const FVector& P, double Time)
{
    // Turbulence magnitude drives the resonance counter-input in rad/s.
    const FVector Wind = Sample(P, Time);
    const double BaseSpeed = 2.5 + 1.5 * FMath::PerlinNoise1D((float)(Time * 0.015 + 40.0));
    return FMath::Clamp((Wind - FVector(0, 0, Wind.Z)).Size() * 0.6 + FMath::Abs(Wind.Z) * 0.4, 0.0, 4.0);
}

double Gustiness(const FVector& P, double Time)
{
    return FMath::Clamp(Sample(P, Time).Size() / 12.0, 0.0, 1.0);
}

double WindwardLift(const FVector& P, const FVector& Wind, FHeightSampler Height)
{
    const double W = FMath::Sqrt(Wind.X * Wind.X + Wind.Y * Wind.Y);
    if (W < 0.25 || !Height) return 0.0;

    // Windward lift lives on the face the air piles against — which is downwind
    // of a bird riding the upwash. Probe downwind (with the wind) so the obstacle
    // ahead of the airflow (sea wall, headland, ridge, treeline, building) reads
    // correctly on its windward side, not the sheltered lee. Work in metres.
    const FVector2D Down = FVector2D(Wind.X, Wind.Y).GetSafeNormal();
    const double X = P.X / 100.0, Y = P.Y / 100.0, Z = P.Z / 100.0;
    const double Ground = Height(P.X, P.Y) / 100.0;   // metres under the bird

    // Upwash is a foot-of-obstacle effect: its reach is a few obstacle-heights,
    // so a distant ridge never lifts a bird kilometres away. The column rises
    // with the obstacle and fades once the bird tops the crest. Higher terrain
    // (more relief) always deflects more air — the tallest/steepest obstacle
    // ahead dominates locally.
    double Best = 0.0;
    for (double D : { 2.0, 4.0, 8.0, 16.0, 32.0, 64.0, 128.0, 256.0, 512.0, 1024.0, 2048.0, 4096.0 })
    {
        const double H = Height((X + Down.X * D) * 100.0, (Y + Down.Y * D) * 100.0) / 100.0;
        const double Relief = H - Ground;
        if (Relief <= 0.0) continue;
        if (D > Relief * 4.0) continue;   // beyond the upwash reach
        const double Falloff = FMath::Exp(-FMath::Max(0.0, Z - H) / FMath::Max(40.0, Relief * 0.75));
        Best = FMath::Max(Best, (Relief / (D + Relief)) * Falloff);
    }
    return FMath::Clamp(W * Best * 2.5, 0.0, 18.0);
}
}