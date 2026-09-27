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

    // Ridge lift: a weak orographic boost on the upwind side of the valley's
    // higher shoulders (the same signed-hill signal as Born2FlapValley::Height).
    const double Shoulder = FMath::Max(0.0, FMath::Abs(Y) - 100.0);
    const double Ridge = FMath::Clamp(Shoulder * 0.02, 0.0, 2.5);
    const double Lift = Ridge * FMath::Clamp(FMath::PerlinNoise3D(FVector((float)(X * 0.004), (float)(Y * 0.004), (float)(Time * 0.05))), 0.0, 1.0);

    FVector Wind = Base + Gust * 3.0;
    Wind.Z = Updraft + Lift + Gust.Z * 0.6;
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
}
