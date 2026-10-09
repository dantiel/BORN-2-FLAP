# Clean membrane and servo audio

The live synth renders the wing as a papery melange: a deep phase-locked
membrane (60–96 Hz fundamental, pitched by airspeed) carrying seven partials
(harmonic core plus inharmonic drum-membrane modes), driven by a soft
zero-slope pressure envelope. Layered on top is a papery broad-band whoosh — a
smooth early-attack swell (sin^0.8·exp, "phi activates early") gating a rustle
that is high-passed above ~650 Hz and low-passed at ~3–5 kHz with one-pole
(non-resonant) filters, so it fizzles like flapped paper rather than snapping
or ringing. The whoosh rides flap ferocity and is added after the low-pass so
its brightness survives; stall reshapes the spectrum (stalled wings chug low and
flutter, fast clean wings sing bright). A DC-blocking high-pass keeps the mix
Air-relative speed raises the bass pitch; the rhythm is driven by the per-strip
aerodynamic force, not a wingbeat clock. The same membrane also sheds a
*flow-separation flutter* whenever it
works the air: broad-band "paper-tearing" noise (high-passed ~900 Hz, low-passed
~3–5 kHz, one-pole and non-resonant so it fizzles rather than rings), excited by
dynamic pressure past a transient threshold AND by the wing's own angular
velocity (flap strokes + fast manoeuvres). In a steady glide the flutter is a
UNIFORM swoosh — a constant, non-pulsing hiss — and its transient/bursty quality
comes only from the motion term (a flap stroke or pitch flick spikes the wing's
angular velocity and gates the hiss in bursts). Because the SAME excitation
drives it, a fast glide and a quick pitch flick sound like flapping — the
flutter is present in glide, flap and manoeuvre alike, not gated to the stroke.
The glide's signature is a continuous aero *whistle* — a narrowband tone pitched
by airspeed (≈380 + 46·speed Hz) with a slow vibrato and band-limited breath —
plus a steady, flat-envelope air *whoosh* (broadband noise, airspeed-gated). Its
only rhythmic content is a SUBTLE wingbeat pump — a ±10 % amplitude swell at the
measured flap rate that is carried entirely by the whoosh/paper noise bed (never
a tonal pulse), so a flapping wing breathes gently without the hard on/off knock
of the old sin² downstroke pulse; in a pure glide the pump vanishes to a flat
stream (beat → 0). The tonal membrane body (the downstroke thump) is gated by
the real per-strip aerodynamic force (`wing_force`), not a wingbeat clock — so a
pure glide produces NO membrane knock at all and only the steady
whoosh + whistle + flutter remain: a unified swoosh, not a "knock… knock…"
cranking engine. The percussive thump (and the papery-whoosh rustle) follows
the actual downstroke force transient. A resting wing is silent (force → 0).
The *leaves* voice
is a steady aperiodic rustle — high-passed ~1.5 kHz and low-passed ~6 kHz, its
amplitude breathing through a slow low-passed noise swell (~0.3–1.5 Hz cutoff,
scaled by turbulence). It must NOT pulse on a periodic sine: an earlier
0.5+0.5·sin(2π(2+5·turb)·t) envelope pumped the foliage fully on/off at 2–7 Hz,
which read as a phantom wingbeat / "motorcycle engine" knock even in a pure
glide (the wing voice was silent; the leaves pump was the whole artifact).
broad metallic resonances around 2–6 kHz, modulated by gear movement and brief
twitches. There is no sustained high sine carrier. A lower harmonic
motor/gearcase drone grows stronger and drops in pitch under load. Left and
right gears have slight deterministic tuning differences, and all movement
parameters remain smoothed.

Two defects contributed to the previous crackling sound: the noise conversion
produced values from -1 to +3 instead of -1 to +1, and servo oscillators used
instantaneous frequency multiplied by elapsed time. That sweep generated
increasingly aliased frequencies. Noise is now unbiased, oscillator phases are
integrated continuously, and gain, pitch and movement parameters are smoothed
at audio rate. Reset clears oscillator, filter and random-generator state.
Existing camera attenuation and mix headroom remain in place.

Build and inspect the isolated voices without Unreal:

```powershell
cmake -S Tools/audio_quality -B .setup/audio-quality-build -G "Visual Studio 17 2022" -A x64
cmake --build .setup/audio-quality-build --config Release
./.setup/audio-quality-build/Release/audio_quality.exe .setup/audio-quality
python Tools/audio_quality/check_spectrum.py .setup/audio-quality
```

This produces `wing.wav`, `servo.wav`, `wind.wav` and `mix.wav`, and checks finite
samples, headroom, deterministic reset, DC offset, click transients, membrane
frequency content, broad servo spectra, airspeed-dependent wing pitch and
load-dependent lower drone energy. It also produces `wing_slow.wav`,
`wing_fast.wav`, `servo_light.wav` and `servo_loaded.wav`. Run
`Unreal/Born2Flap/Tools/test-audio.ps1` after rebuilding the editor target to
check the real Unreal synthesis callback. These checks do not replace listening
and adjusting the balance on the player's speakers/headphones.