#pragma once
// Born2FlapWind.h — the atmospheric field: the third player in the valley.
//
// A procedurally-animated wind field (base wind + gust turbulence + drifting
// thermal columns + windward lift) sampled at any world point and time. It is
// the source of truth that feeds BOTH the flight physics (air-relative velocity +
// phase-noise the resonance layer must counter) and the aero-audio voices
// (wind hiss, wing-tone breakage, thermal leaf rustle, servo strain).

#include "CoreMinimal.h"

namespace Born2FlapWind
{
    // Atmospheric wind at world position P (centimetres) and time T (seconds).
    // Returns wind in metres/second, Unreal world axes (X/Y horizontal, Z up).
    FVector Sample(const FVector& P, double Time);

    // Turbulent wind-phase noise [rad/s] the MathCore resonance layer must
    // counter (b2f_math_set_wind_phase_noise). Scaled from gust magnitude.
    double PhaseNoise(const FVector& P, double Time);

    // Normalised 0..1 wind strength for vegetation/particle visual sway.
    double Gustiness(const FVector& P, double Time);

    // Height sampler: world X,Y (centimetres) -> obstacle-top height (centimetres).
    // Terrain, buildings and forest canopy all read through this so wind can
    // pile up against any of them alike.
    using FHeightSampler = double (*)(double X, double Y);

    // Windward lift [m/s, +Z]: horizontal wind deflected upward as it piles
    // against upwind terrain, buildings and trees. Wind = horizontal wind (m/s);
    // Height = the level's obstacle-top sampler. Zero when calm, unsampled, or
    // on the sheltered (lee) side of the obstacle.
    double WindwardLift(const FVector& P, const FVector& Wind, FHeightSampler Height);
}
