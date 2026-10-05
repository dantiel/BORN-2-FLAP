# Clean membrane and servo audio

The live synth uses damped membrane pressure pulses around 34–60 Hz for
wingbeats, with a broad soft envelope and very little upper-frequency content.
Air-relative speed raises the bass pitch; wingbeat rate controls the rhythm.
Zero throttle silences the stroke voice. Servos use noise-excited broad metallic
resonances around 2–6 kHz, modulated by gear movement and brief twitches.
There is no sustained high sine carrier. A lower harmonic motor/gearcase drone
grows stronger and drops in pitch under load. Left and right gears have slight
deterministic tuning differences, and all movement parameters remain smoothed.

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
