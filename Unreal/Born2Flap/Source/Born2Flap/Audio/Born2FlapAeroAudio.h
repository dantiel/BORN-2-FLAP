#pragma once
// Born2FlapAeroAudio.h — the live C++ mirror of Brain/lib/born2flap/audio/aero_audio.rb.
//
// The Ruby engine is the canonical "physics in one place" reference and headless
// proof. This header is its LIVE counterpart: the SAME pure mapping
// (Telemetry → per-voice spectral params), so the flight pawn can drive the
// synth each tick from real firmware output + wind + camera perspective without
// a Ruby process in the loop. It is dependency-free (only Born2FlapUiOps.h) so
// it could be headless-tested exactly like the Ruby engine.
//
//   firmware output ─► Telemetry ─► Mix ─► {wind,wing,servo_l,servo_r,leaves}
//   wind field   ─┘
//   camera       ─┘

#include "UI/Born2FlapUiOps.h"
#include <cmath>
#include <string>

namespace born2flap::aeroaudio {

// Immutable snapshot of physics telemetry — mirrors AeroAudio::Telemetry.
struct FTelemetry {
    double wingbeat_hz = 3.0;
    double airspeed = 0.0;
    double altitude = 0.0;
    double thermal_strength = 0.0;
    double servo_load_l = 0.0;
    double servo_load_r = 0.0;
    double sweep_rate = 0.0;        // deg/s servo position change (chirp driver)
    double phase_error_rad = 0.0;   // MathCore resonance δ (rad)
    double k_gain_mod = 1.0;        // phase-advance demand
    double stall_margin = 1.0;      // 1 far from stall, 0 stalled
    // perspective (listener-relative)
    double listener_distance = 5.0; // metres
    double listener_bearing = 0.0;  // rad, 0 = straight ahead
    double approach_speed = 0.0;    // m/s along listener axis (+ = approaching)
};

inline double Clamp(double V, double Lo, double Hi) { return V < Lo ? Lo : (V > Hi ? Hi : V); }
inline double Clamp01(double V) { return Clamp(V, 0.0, 1.0); }

inline void SetNum(ui::FProps& P, const char* K, double V) {
    ui::FValue FV;
    FV.kind = ui::FValue::Kind::Number;
    FV.num = V;
    P[K] = FV;
}
inline double GetNum(const ui::FProps& P, const char* K, double Dflt = 0.0) {
    auto It = P.find(K);
    return It != P.end() ? It->second.AsNumber(Dflt) : Dflt;
}

struct FPerspective { double Att, Doppler, Pan, Damp; };

inline FPerspective Perspective(const FTelemetry& T) {
    const double Dist = std::max(T.listener_distance, 0.5);
    FPerspective P;
    P.Att = 1.0 / (1.0 + (Dist / 10.0) * (Dist / 10.0));
    P.Doppler = Clamp((343.0 + T.approach_speed) / 343.0, 0.6, 1.6);
    P.Pan = Clamp(std::sin(T.listener_bearing), -1.0, 1.0);
    P.Damp = Clamp01(1.0 - Dist / 200.0);
    return P;
}

// A servo voice is the same physics, only its hinge-load input differs.
inline void ServoVoice(ui::FProps& P, double Load, const FTelemetry& T) {
    SetNum(P, "gain", Clamp01(0.3 + 0.7 * Load));
    SetNum(P, "chirp", Clamp01(T.sweep_rate / 400.0));
    SetNum(P, "groan", Clamp01(Load));
    SetNum(P, "strain", Clamp01(1.0 - T.stall_margin));
    SetNum(P, "sweep", 6.0 + 10.0 * Clamp01(T.sweep_rate / 400.0));
    SetNum(P, "groan_pitch", 90.0 - 40.0 * Load);
    SetNum(P, "brightness", Clamp01(1.0 - Load));
}

// Intrinsic spectral transformation per voice (before perspective).
inline ui::FProps ComputeIntrinsic(const std::string& Voice, const FTelemetry& T) {
    ui::FProps P;
    if (Voice == "wind") {
        SetNum(P, "gain", Clamp01(std::pow(T.airspeed / 30.0, 3.0)));
        SetNum(P, "brightness", Clamp01(T.airspeed / 40.0));
        SetNum(P, "gust_depth", Clamp01(0.1 + 0.6 * T.thermal_strength));
        SetNum(P, "gust_rate", 0.4 + 0.6 * T.wingbeat_hz);
    } else if (Voice == "wing") {
        SetNum(P, "rate", T.wingbeat_hz);
        SetNum(P, "air_speed", T.airspeed);
        SetNum(P, "gain", Clamp01((T.wingbeat_hz / 8.0) * (0.3 + 0.7 * Clamp01(T.airspeed / 20.0))));
        SetNum(P, "tone", Clamp01(1.0 - std::fabs(T.phase_error_rad) / 0.5));
        SetNum(P, "whoosh", Clamp01(T.airspeed / 25.0));
        SetNum(P, "brightness", Clamp01(T.airspeed / 40.0));
    } else if (Voice == "servo_l") {
        ServoVoice(P, T.servo_load_l, T);
    } else if (Voice == "servo_r") {
        ServoVoice(P, T.servo_load_r, T);
    } else if (Voice == "leaves") {
        const double Foliage = Clamp01(1.0 - T.altitude / 50.0);
        SetNum(P, "gain", Clamp01(T.airspeed / 20.0) * Foliage);
        SetNum(P, "turbulence", Clamp01(0.15 + 0.7 * T.thermal_strength));
        SetNum(P, "brightness", 1.0);
    }
    return P;
}

inline void ApplyPerspective(ui::FProps& P, const FTelemetry& T) {
    const FPerspective Per = Perspective(T);
    SetNum(P, "gain", GetNum(P, "gain", 0.0) * Per.Att);
    SetNum(P, "pitch", Per.Doppler);
    SetNum(P, "pan", Per.Pan);
    if (P.count("brightness"))
        SetNum(P, "brightness", GetNum(P, "brightness") * Per.Damp);
}

inline ui::FProps Compute(const std::string& Voice, const FTelemetry& T) {
    ui::FProps P = ComputeIntrinsic(Voice, T);
    ApplyPerspective(P, T);
    return P;
}

// All five voices at once, in a fixed order matching the voice-name table below.
inline void Mix(const FTelemetry& T, ui::FProps Out[5]) {
    Out[0] = Compute("wind", T);
    Out[1] = Compute("wing", T);
    Out[2] = Compute("servo_l", T);
    Out[3] = Compute("servo_r", T);
    Out[4] = Compute("leaves", T);
}

inline const char* const* VoiceNames() {
    static const char* Names[5] = {"wind", "wing", "servo_l", "servo_r", "leaves"};
    return Names;
}

}  // namespace born2flap::aeroaudio
