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
//   wing   → soft membrane swell + papery broad-band whoosh, pitched by airspeed.
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
    } gearA,gearB,refLow,refHigh;
    FProps p;
    uint32_t rng = 0x9E3779B9u;
    double lp = 0.0;   // low-pass state (primary)
    double lp2 = 0.0;  // low-pass state (cascade / band-pass)
    double gain=0, pitch=1, pan=0, rate=0, tone=.5, brightness=0;
    double chirp=0, load=0, strain=0, whoosh=0;
        double stroke=0, phase1=0, phase2=0, phase3=0, phase4=0, gearPhase=0;
    double phase5=0, phase6=0, phase7=0, crack=0, stall=0;
    double flutter=0, flutterLp=0, sepLp=0, gustLp=0, forceLp=0;
    double airspeed=0, damp1=0, damp2=0, paperLp1=0, paperLp2=0;
    double whistlePhase=0, whistleVib=0, whistleHp=0, whistleLp=0, whistleAmp=0;
    double beatLp=0, beatPhase=0;
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
        // DC-blocking high-pass (one-pole, ~20 Hz): keeps the bus free of any
        // residual mean so asymmetric envelopes can't pump a DC offset into the
        // output. Corner sits far below the 60 Hz membrane, so the bass is intact.
        accL = DcBlock(accL, dcXL, dcYL);
        accR = DcBlock(accR, dcXR, dcYR);
        outL = (float)clampRaw(accL * 0.6, -1.0, 1.0);
        outR = (float)clampRaw(accR * 0.6, -1.0, 1.0);
        t += 1.0 / sr;
    }

    // Restart the shared clock (for deterministic re-renders).
    void Reset() {
        t=0;
        dcXL=dcYL=dcXR=dcYR=0;
        for(auto& entry:voices) {
            auto params=entry.second.p;auto seed=entry.second.seed;
            entry.second=FVoice{};entry.second.p=params;entry.second.seed=entry.second.rng=seed;
        }
    }

private:
    double sr;
    double t = 0.0;
    double dcXL=0, dcYL=0, dcXR=0, dcYR=0;
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
    // One-pole DC-blocking high-pass (~20 Hz at 44.1 kHz). Removes any residual
    // mean (asymmetric envelopes, low-frequency pumping) without touching the
    // 60 Hz+ membrane.
    double DcBlock(double x, double& xm1, double& ym1) {
        const double y = x - xm1 + 0.997 * ym1;
        xm1 = x;
        ym1 = y;
        return y;
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
        // Allow gain up to 4 so the wingbeat's 0..4 user volume can reach 4×.
        // Ambient voices are already clamped to [0,1] in ComputeIntrinsic, so only
        // "wing" exceeds 1.
        double gain = LowPass(v.gain,clamp(p,"gain",0,4),12);
        double brightness=LowPass(v.brightness,clamp(p,"brightness",0,1),10);
        std::string kind = "wind";
        // Determine the voice kind from its distinct params (cheap tag).
        if (p.count("gust_depth"))     kind = "wind";
        else if (p.count("whoosh"))    kind = "wing";
        else if (p.count("groan"))     kind = "servo";
        else if (p.count("turbulence")) kind = "leaves";

        if (kind == "wind") {
            double n = Noise(v);
            double cutoff = (180.0 + 3200.0 * brightness) * pitch;
            double f = LowPass(v.lp, n, cutoff);
            // Aperiodic slow swell, NOT a flap-rate sine pump: the air is a
            // constant stream whose amplitude breathes gently and randomly.
            double gust = 1.0 + num(p, "gust_depth", 0.0) * LowPass(v.gustLp, Noise(v), 0.35);
            return f * gain * gust * 0.4;
        }
        if (kind == "wing") {
            double tone=LowPass(v.tone,clamp(p,"tone",0,1),9);
            double air=LowPass(v.whoosh,clamp(p,"whoosh",0,1),9);
            double crack=LowPass(v.crack,clamp(p,"crack",0,1),9);
            double stall=LowPass(v.stall,clamp(p,"stall",0,1),8);
            double flutter=LowPass(v.flutter,clamp(p,"flutter",0,1),8);
            double whistle=LowPass(v.whistleAmp,clamp(p,"whistle",0,1),9);
            // SUBTLE WINGBEAT PUMP — carried by the noise bed only. The per-strip
            // force remains the sound SOURCE; this merely breathes the whoosh a
            // few percent at the measured flap rate so the stream feels alive
            // without the hard on/off knock of the old sin² downstroke pulse.
            // In a pure glide (beat→0) sin(0)=0 and the stream stays constant.
            double beat=LowPass(v.beatLp,clampRaw(num(p,"beat",0.0),0.0,12.0),6);
            Advance(v.beatPhase,beat);
            const double pump=1.0+0.10*std::sin(v.beatPhase);
            // Force envelope: the per-strip aerodynamic force drives EVERYTHING.
            // A steady force → a constant stream; a flapping force → the natural
            // transient; flow separation → the high-frequency flutter. There is
            // NO synthetic wingbeat clock and NO sin² downstroke pulse.
            double force=LowPass(v.forceLp,clampRaw(num(p,"wing_force",air),0,1),40);
            const double speed=LowPass(v.airspeed,clampRaw(num(p,"air_speed",brightness*40),0,60),4);
            const double fundamental=(60+.9*std::min(speed,40.))*pitch;
            // RICH SPECTRUM — a broad membrane: harmonic core plus inharmonic
            // drum-membrane modes (the physical wing body).
            Advance(v.phase1,fundamental);
            Advance(v.phase2,fundamental*1.55);
            Advance(v.phase3,fundamental*2.10);
            Advance(v.phase4,fundamental*2.76);
            Advance(v.phase5,fundamental*3.10);
            Advance(v.phase6,fundamental*3.90);
            Advance(v.phase7,fundamental*4.70);
            const double lowBody=std::sin(v.phase1)+.38*std::sin(v.phase2);
            const double upBody=.24*std::sin(v.phase3)+.16*std::sin(v.phase4)
                              +.11*std::sin(v.phase5)+.07*std::sin(v.phase6)
                              +.05*std::sin(v.phase7);
            const double lowW=.9+.5*stall;
            const double upW=.55+.6*(1.0-stall)+.4*clampRaw(speed/40.0,0.0,1.0);
            const double membrane=(lowBody*lowW+upBody*upW)*.58;
            // Air whoosh — broad, steady, airspeed-gated. The constant stream:
            // a flat envelope that only breathes through the subtle noise-carried
            // pump above — never a hard on/off pulse.
            const double airNoise=LowPass(v.lp2,LowPass(v.lp,Noise(v),120+speed*8),350);
            const double glideWhoosh=airNoise*air*.4*pump;
            // Papery whoosh — a BROAD rustle gated by the FORCE (a working wing
            // fizzles), high-passed ~650 Hz then low-passed ~3–5 kHz, one-pole
            // and non-resonant so it fizzles instead of ringing.
            const double paperN=Noise(v);
            const double paperHp=paperN-LowPass(v.paperLp1,paperN,650.0*pitch);
            const double paper=LowPass(v.paperLp2,paperHp,(3200+1600*crack+12*speed)*pitch);
            const double paperWhoosh=paper*force*(.14+.34*crack)*pump;
            // Tonal membrane — gated by force directly. When the wing stops
            // working the air (perched/clean glide) the membrane falls silent;
            // only the steady whoosh + whistle remain.
            const double body=membrane*force*(.8+.2*tone);
            // Stall darkens the top end; speed reopens it.
            const double cut=(105+speed)*(1.0-.45*stall);
            const double smooth=LowPass(v.damp2,LowPass(v.damp1,body,cut),cut);
            // FLOW-SEPARATION FLUTTER — broadband "paper-tearing" noise driven
            // by the actual force fluctuation (wing_flutter), NOT a static
            // airspeed/sweep guess. Broad band (high-pass then low-pass, one-pole,
            // NO resonance → no ring).
            const double sepN = Noise(v);
            const double sepHp = sepN - LowPass(v.flutterLp, sepN, 900.0 * pitch);
            const double sepBand = LowPass(v.sepLp, sepHp, (3000.0 + 2400.0 * flutter) * pitch);
            const double sepNoise = sepBand * flutter * flutter;
            // WHISTLE — a continuous narrowband aero tone (air through feather
            // slots). Dominant in a fast glide: a uniform whistle, not a knock.
            const double whistleHz=(380.0+46.0*speed)*pitch;
            Advance(v.whistleVib, 5.5);
            const double vib=1.0+0.02*std::sin(v.whistleVib);
            Advance(v.whistlePhase, whistleHz*vib);
            const double whistleTone=std::sin(v.whistlePhase)
                                    +.35*std::sin(v.whistlePhase*2.01)
                                    +.16*std::sin(v.whistlePhase*2.98);
            const double wNoise=Noise(v);
            const double wHp=wNoise-LowPass(v.whistleHp,wNoise,whistleHz*0.5);
            const double whistleBreath=LowPass(v.whistleLp,wHp,whistleHz*2.2);
            const double whistleVoice=(.72*whistleTone+.28*whistleBreath)*whistle*0.32;
            return glideWhoosh + (smooth + paperWhoosh) * gain * 1.9 + sepNoise * 0.55 + whistleVoice;
        }
        if (kind == "servo") {
            const double motion=LowPass(v.chirp,clamp(p,"chirp",0,1),16);
            const double load=LowPass(v.load,clamp(p,"groan",0,1),10);
            const double strain=LowPass(v.strain,clamp(p,"strain",0,1),8);
            const double sweep=LowPass(v.rate,clampRaw(num(p,"sweep",8),2,24),10);
            Advance(v.gearPhase,sweep);
            const double detune=1+((v.seed&255)/255.0-.5)*.008;
            const double n=Noise(v);
            // Gear mesh only while the servo actually turns: strictly motion-gated
            // so a resting wing is silent (no idle hiss/whistle floor).
            const double metal=1.2*BandPass(v.gearA,n,(2200+1500*motion)*pitch*detune,1.5)
                              +.55*BandPass(v.gearB,n,(4300+1200*motion)*pitch*detune,1.9);
            Advance(v.phase1,(650+800*motion)*pitch);
            const double teeth=.72+.28*std::cos(v.phase1);
            const double twitch=.48+.52*std::pow(.5+.5*std::sin(v.gearPhase),5);
            const double gears=LowPass(v.lp2,LowPass(v.lp,metal*teeth,7000),7000)*twitch*motion*.4;
            // Metallic drone: inharmonic partials (metal-bar ratios 1 : 2.76 : 5.40)
            // instead of a harmonic 2:1 pipe tone, so it reads as brushed metal
            // rather than a wooden flute.
            const double whineHz=(360+820*motion)*pitch*detune;
            Advance(v.phase2,whineHz);
            const double drone=(std::sin(v.phase2)+.32*std::sin(v.phase2*2.76)
                               +.14*std::sin(v.phase2*5.40))*motion*.2*(.6+.4*(1-load));
            // Low- and high-pitch noise refraction: two narrow metallic bands that
            // shimmer against the drone (the "touch of high + low" refraction).
            const double refractLow=BandPass(v.refLow,Noise(v),(210+170*motion)*pitch*detune,2.2)*motion*.45;
            const double refractHigh=BandPass(v.refHigh,Noise(v),(5200+1400*motion)*pitch*detune,2.6)*motion*.30;
            // Motor/gearcase drone still droops under load, kept subtle.
            const double motorHz=(150+40*motion-65*load)*pitch*detune;
            Advance(v.phase3,motorHz);
            const double motor=(std::sin(v.phase3)+.28*std::sin(v.phase3*2.01))
                              *load*(.05+.12*load+.035*strain)*(.8+.2*motion);
            return (gears+drone+refractLow+refractHigh+motor)*gain;
        }
        // leaves
        double n = Noise(v);
        double hp = n - LowPass(v.lp, n, 1500.0 * pitch);
        double rustle = LowPass(v.lp2, hp, 6000.0 * pitch);
        double turb = num(p, "turbulence", 0.0);
        // Steady aperiodic rustle, NOT a periodic sine pump. The old
        // 0.5+0.5·sin(2π(2+5·turb)·t) pulsed the foliage fully on/off at
        // 2–7 Hz — an amplitude-modulated "motorcycle engine" knock that
        // persisted even in a pure glide. A slow low-passed noise swell
        // (cutoff ~0.3–1.5 Hz) keeps the leaves breathing gently without
        // any rhythmic beat.
        double gust = LowPass(v.gustLp, Noise(v), 0.3 + 1.2 * turb);
        double env = 0.6 + 0.4 * gust;
        return rustle * gain * env * 0.5;
    }
    void Advance(double& phase,double hz) {
        phase+=2*AudioPi*hz/sr;
        if(phase>=2*AudioPi) phase-=2*AudioPi;
    }
};

}  // namespace born2flap::audio