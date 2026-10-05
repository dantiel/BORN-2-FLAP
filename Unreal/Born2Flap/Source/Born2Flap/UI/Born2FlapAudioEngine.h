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
//   wing   → soft, damped 34–60 Hz pressure pulses, pitched by airspeed.
//   servo  → broadband metallic gear texture and a load-dependent low drone.
//            Phase is integrated continuously, including changing telemetry.
//   leaves → band-limited rustle noise, turbulence-modulated.
//
// Perspective is baked in by the Brain: `gain` (distance), `pitch` (Doppler),
// `pan` (bearing), `brightness` rolloff (air absorption).

#include "Born2FlapUiOps.h"
#include <cmath>
#include <map>
#include <string>
#include <algorithm>

namespace born2flap::audio {

inline constexpr double AudioPi = 3.14159265358979323846;

using born2flap::ui::FProps;

struct FVoice {
    struct FBand {
        double x1=0,x2=0,y1=0,y2=0,hz=-1,b0=0,b2=0,a1=0,a2=0;
    } gearA,gearB;
    FProps p;
    uint32_t rng = 0x9E3779B9u;
    double lp = 0.0;   // low-pass state (primary)
    double lp2 = 0.0;  // low-pass state (cascade / band-pass)
    double gain=0, pitch=1, pan=0, rate=0, tone=.5, brightness=0;
    double chirp=0, load=0, strain=0, whoosh=0;
    double stroke=0, phase1=0, phase2=0, phase3=0, gearPhase=0;
    double airspeed=0, damp1=0, damp2=0;
    uint32_t seed=0x9E3779B9u;
};

class FAudioEngine {
public:
    explicit FAudioEngine(double sampleRate = 44100.0) : sr(sampleRate) {}

    void SetParams(const std::string& name, const FProps& params) {
        auto result=voices.try_emplace(name);
        auto& voice=result.first->second;
        if(result.second) {
            for(unsigned char c:name) voice.seed=(voice.seed^c)*16777619u;
            voice.rng=voice.seed;
        }
        voice.p = params;
    }

    double SampleRate() const { return sr; }

    // Advance one frame and produce a stereo (L/R) sample pair in [-1, 1].
    void RenderFrame(float& outL, float& outR) {
        double accL = 0.0, accR = 0.0;
        for (auto& kv : voices) {
            double mono = RenderVoice(kv.second);
            double pan = LowPass(kv.second.pan,clamp(kv.second.p, "pan", -1.0, 1.0),10);
            double ang = (pan + 1.0) * 0.25 * AudioPi;  // 0..π/2 constant-power
            accL += mono * std::cos(ang);
            accR += mono * std::sin(ang);
        }
        outL = (float)clampRaw(accL * 0.6, -1.0, 1.0);
        outR = (float)clampRaw(accR * 0.6, -1.0, 1.0);
        t += 1.0 / sr;
    }

    // Restart the shared clock (for deterministic re-renders).
    void Reset() {
        t=0;
        for(auto& entry:voices) {
            auto params=entry.second.p;auto seed=entry.second.seed;
            entry.second=FVoice{};entry.second.p=params;entry.second.seed=entry.second.rng=seed;
        }
    }

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
        return ((double)(v.rng & 0xFFFFFFu) / 0xFFFFFFu) * 2.0 - 1.0;  // unbiased [-1,1]
    }

    // One-pole low-pass with per-voice state.
    double LowPass(double& state, double x, double cutoffHz) {
        double dt = 1.0 / sr;
        double rc = 1.0 / (2.0 * AudioPi * std::max(1.0, cutoffHz));
        double a = dt / (dt + rc);
        state += a * (x - state);
        return state;
    }
    // Broad mechanical resonances excited by noise, rather than narrow sine
    // carriers. Low Q avoids the whistle of the previous servo voice.
    double BandPass(FVoice::FBand& b,double x,double hz,double q) {
        hz=clampRaw(hz,100,sr*.32);
        if(std::abs(hz-b.hz)>2) {
            const double w=2*AudioPi*hz/sr,alpha=std::sin(w)/(2*q),a0=1+alpha;
            b.b0=alpha/a0;b.b2=-b.b0;b.a1=-2*std::cos(w)/a0;b.a2=(1-alpha)/a0;b.hz=hz;
        }
        const double y=b.b0*x+b.b2*b.x2-b.a1*b.y1-b.a2*b.y2;
        b.x2=b.x1;b.x1=x;b.y2=b.y1;b.y1=y;return y;
    }

    double RenderVoice(FVoice& v) {
        const FProps& p = v.p;
        double pitch = LowPass(v.pitch,clampRaw(num(p,"pitch",1),.5,2),8);
        double gain = LowPass(v.gain,clamp(p,"gain",0,1),12);
        double brightness=LowPass(v.brightness,clamp(p,"brightness",0,1),10);
        std::string kind = "wind";
        // Determine the voice kind from its distinct params (cheap tag).
        if (p.count("gust_rate"))      kind = "wind";
        else if (p.count("whoosh"))    kind = "wing";
        else if (p.count("groan"))     kind = "servo";
        else if (p.count("turbulence")) kind = "leaves";

        if (kind == "wind") {
            double n = Noise(v);
            double cutoff = (180.0 + 3200.0 * brightness) * pitch;
            double f = LowPass(v.lp, n, cutoff);
            double gust = 1.0 + num(p, "gust_depth", 0.0) * 0.5
                              * std::sin(2.0 * AudioPi * num(p, "gust_rate", 1.0) * t);
            return f * gain * gust * 0.4;
        }
        if (kind == "wing") {
            double rate=LowPass(v.rate,clampRaw(num(p,"rate",3),0,12)*pitch,9);
            double tone=LowPass(v.tone,clamp(p,"tone",0,1),9);
            double air=LowPass(v.whoosh,clamp(p,"whoosh",0,1),9);
            Advance(v.stroke,rate);
            const double q=v.stroke/(2*AudioPi);
            const double speed=LowPass(v.airspeed,clampRaw(num(p,"air_speed",brightness*40),0,60),4);
            // Broad, zero-slope pressure envelope: softly cushioned air, with
            // the body mostly in the lowest bass and little upper resonance.
            const double env=3.5*std::pow(std::sin(AudioPi*q),2)*std::exp(-q*2.5);
            const double fundamental=(34+.75*std::min(speed,35.))*pitch;
            Advance(v.phase1,fundamental);
            Advance(v.phase2,fundamental*1.593);
            Advance(v.phase3,fundamental*2.136);
            const double membrane=(std::sin(v.phase1)+.12*std::sin(v.phase2)+.035*std::sin(v.phase3))*.76;
            const double airNoise=LowPass(v.lp2,LowPass(v.lp,Noise(v),120+speed*8),350);
            const double body=membrane*env*(.8+.2*tone)+airNoise*env*air*.16;
            return LowPass(v.damp2,LowPass(v.damp1,body,105+speed),105+speed)*gain*.52;
        }
        if (kind == "servo") {
            const double motion=LowPass(v.chirp,clamp(p,"chirp",0,1),16);
            const double load=LowPass(v.load,clamp(p,"groan",0,1),10);
            const double strain=LowPass(v.strain,clamp(p,"strain",0,1),8);
            const double sweep=LowPass(v.rate,clampRaw(num(p,"sweep",8),2,24),10);
            Advance(v.gearPhase,sweep);
            const double detune=1+((v.seed&255)/255.0-.5)*.008;
            const double n=Noise(v);
            const double metal=1.6*BandPass(v.gearA,n,(2400+1200*motion)*pitch*detune,1.25)
                              +.7*BandPass(v.gearB,n,(4900+600*motion)*pitch*detune,1.8);
            Advance(v.phase1,(650+700*motion)*pitch);
            const double teeth=.72+.28*std::cos(v.phase1);
            const double twitch=.48+.52*std::pow(.5+.5*std::sin(v.gearPhase),5);
            const double gears=LowPass(v.lp2,LowPass(v.lp,metal*teeth,6500),6500)*twitch*motion*.30;
            // Motor/gearcase drone acquires weight and droops under load.
            const double motorHz=(170+35*motion-65*load)*pitch*detune;
            Advance(v.phase2,motorHz);Advance(v.phase3,motorHz*2.01);
            const double motor=(std::sin(v.phase2)+.28*std::sin(v.phase3)+.10*std::sin(3*v.phase2))
                              *load*(.045+.115*load+.035*strain)*(.8+.2*motion);
            return (gears+motor)*gain;
        }
        // leaves
        double n = Noise(v);
        double hp = n - LowPass(v.lp, n, 1500.0 * pitch);
        double rustle = LowPass(v.lp2, hp, 6000.0 * pitch);
        double turb = num(p, "turbulence", 0.0);
        double env = 0.5 + 0.5 * std::sin(2.0 * AudioPi * (2.0 + 5.0 * turb) * t);
        return rustle * gain * env * 0.5;
    }
    void Advance(double& phase,double hz) {
        phase+=2*AudioPi*hz/sr;
        if(phase>=2*AudioPi) phase-=2*AudioPi;
    }
};

}  // namespace born2flap::audio
