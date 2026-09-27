#pragma once
// Born2FlapAudioEngine.h — the dependency-free aero-audio SYNTH (host half).
//
// This is the "dumb" renderer for the Ruby aero-audio-physics engine. It has
// NO Unreal dependency: it owns five voices and turns their spectral params
// into PCM, so the SAME header compiles in the headless Tools/audio_render_test
// (→ WAV) and later inside a UMG USynthComponent (real-time playback).
//
// The physics lives in ONE place — Brain/lib/born2flap/audio/aero_audio.rb —
// which publishes `set_audio_params` ops. This engine only consumes them:
//
//   wind   → white noise → low-pass (cutoff ∝ brightness), gust-modulated.
//   wing   → phase-locked fundamental + subharmonic (the "velvet paw") +
//            a once-per-cycle downstroke whoosh; `tone` (resonance δ) weights
//            the clean fundamental against the ragged breath.
//   servo  → a swept, pulse-gated high chirp (cricket) plus a low sawtooth
//            groan that droops in pitch and hardens with distortion as the
//            load approaches stall (`strain`).
//   leaves → band-limited rustle noise, turbulence-modulated.
//
// Perspective is baked in by the Brain: `gain` (distance), `pitch` (Doppler),
// `pan` (bearing), `brightness` rolloff (air absorption).

#include "Born2FlapUiOps.h"
#include <cmath>
#include <map>
#include <string>

namespace born2flap::audio {

inline constexpr double AudioPi = 3.14159265358979323846;

using born2flap::ui::FProps;

struct FVoice {
    FProps p;
    uint32_t rng = 0x9E3779B9u;
    double lp = 0.0;   // low-pass state (primary)
    double lp2 = 0.0;  // low-pass state (cascade / band-pass)
};

class FAudioEngine {
public:
    explicit FAudioEngine(double sampleRate = 44100.0) : sr(sampleRate) {}

    void SetParams(const std::string& name, const FProps& params) {
        voices[name].p = params;
    }

    double SampleRate() const { return sr; }

    // Advance one frame and produce a stereo (L/R) sample pair in [-1, 1].
    void RenderFrame(float& outL, float& outR) {
        double accL = 0.0, accR = 0.0;
        for (auto& kv : voices) {
            double mono = RenderVoice(kv.second);
            double pan = clamp(kv.second.p, "pan", -1.0, 1.0);
            double ang = (pan + 1.0) * 0.25 * AudioPi;  // 0..π/2 constant-power
            accL += mono * std::cos(ang);
            accR += mono * std::sin(ang);
        }
        outL = (float)clampRaw(accL * 0.6, -1.0, 1.0);
        outR = (float)clampRaw(accR * 0.6, -1.0, 1.0);
        t += 1.0 / sr;
    }

    // Restart the shared clock (for deterministic re-renders).
    void Reset() { t = 0.0; }

private:
    double sr;
    double t = 0.0;
    std::map<std::string, FVoice> voices;

    static double num(const FProps& p, const char* k, double dflt = 0.0) {
        auto it = p.find(k);
        return it != p.end() ? it->second.AsNumber(dflt) : dflt;
    }
    static double clamp(const FProps& p, const char* k, double lo, double hi) {
        double v = num(p, k, 0.0);
        return v < lo ? lo : (v > hi ? hi : v);
    }
    static double clampRaw(double v, double lo, double hi) {
        return v < lo ? lo : (v > hi ? hi : v);
    }

    double Noise(FVoice& v) {
        v.rng ^= v.rng << 13; v.rng ^= v.rng >> 17; v.rng ^= v.rng << 5;
        return ((double)(v.rng & 0xFFFFFFu) / 0x7FFFFFu) * 2.0 - 1.0;  // [-1,1]
    }

    // One-pole low-pass with per-voice state.
    double LowPass(double& state, double x, double cutoffHz) {
        double dt = 1.0 / sr;
        double rc = 1.0 / (2.0 * AudioPi * std::max(1.0, cutoffHz));
        double a = dt / (dt + rc);
        state += a * (x - state);
        return state;
    }

    double RenderVoice(FVoice& v) {
        const FProps& p = v.p;
        double pitch = num(p, "pitch", 1.0);
        double gain = num(p, "gain", 0.0);
        std::string kind = "wind";
        // Determine the voice kind from its distinct params (cheap tag).
        if (p.count("gust_rate"))      kind = "wind";
        else if (p.count("whoosh"))    kind = "wing";
        else if (p.count("groan"))     kind = "servo";
        else if (p.count("turbulence")) kind = "leaves";

        if (kind == "wind") {
            double n = Noise(v);
            double cutoff = (200.0 + 6000.0 * num(p, "brightness", 0.0)) * pitch;
            double f = LowPass(v.lp, n, cutoff);
            double gust = 1.0 + num(p, "gust_depth", 0.0) * 0.5
                              * std::sin(2.0 * AudioPi * num(p, "gust_rate", 1.0) * t);
            return f * gain * gust * 0.4;
        }
        if (kind == "wing") {
            double rate = num(p, "rate", 3.0) * pitch;
            double phi = 2.0 * AudioPi * rate * t;
            double tone = num(p, "tone", 0.5);
            double fund = std::sin(phi) * tone;
            double sub = std::sin(phi * 0.5) * tone * 0.5;
            // once-per-cycle downstroke pulse
            double env = std::pow(std::max(0.0, std::sin(phi + 3.14159265358979323846 * 0.5)), 8.0);
            double n = Noise(v);
            double cutoff = (300.0 + 4000.0 * num(p, "brightness", 0.0)) * pitch;
            double whoosh = LowPass(v.lp, n, cutoff);
            double body = (fund + sub) * (0.4 + 0.6 * tone);
            double breath = whoosh * env * num(p, "whoosh", 0.0) * (0.3 + 0.5 * (1.0 - tone));
            return (body + breath) * gain * 0.5;
        }
        if (kind == "servo") {
            double chirpAmt = num(p, "chirp", 0.0);
            double sweep = num(p, "sweep", 8.0);
            double fchirp = (4000.0 + 3000.0 * std::sin(2.0 * AudioPi * sweep * t)) * pitch;
            double chirp = std::sin(2.0 * AudioPi * fchirp * t);
            double pulse = 0.5 + 0.5 * std::sin(2.0 * AudioPi * 20.0 * t);  // stridulation
            double c = chirp * (0.5 + 0.5 * pulse) * chirpAmt;

            double groan = num(p, "groan", 0.0);
            double strain = num(p, "strain", 0.0);
            double fg = num(p, "groan_pitch", 80.0) * pitch;
            double ph = std::fmod(t * fg, 1.0);
            double saw = 2.0 * ph - 1.0;
            double distorted = std::tanh(2.0 * saw * (1.0 + strain * 3.0));
            double g = distorted * groan * (0.6 + 0.4 * strain);
            double harsh = std::sin(2.0 * AudioPi * fg * 3.0 * t) * strain * 0.3;
            return (c * 0.3 + g * 0.5 + harsh) * gain;
        }
        // leaves
        double n = Noise(v);
        double hp = n - LowPass(v.lp, n, 1500.0 * pitch);
        double rustle = LowPass(v.lp2, hp, 6000.0 * pitch);
        double turb = num(p, "turbulence", 0.0);
        double env = 0.5 + 0.5 * std::sin(2.0 * AudioPi * (2.0 + 5.0 * turb) * t);
        return rustle * gain * env * 0.5;
    }
};

}  // namespace born2flap::audio