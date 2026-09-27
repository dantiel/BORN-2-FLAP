# Live-Audio & der lebendige Himmel

Die aero-audio-Stimme des Ornithopters läuft jetzt **live im Unreal-Audio-Loop**,
gespeist von der echten Flugphysik — nicht mehr aus einem skriptierten Demo.
Dahinter steht ein **atmosphärisches Wind-/Thermik-Feld**, das gleichzeitig die
Physik, den Klang und die sichtbare Welt antreibt.

```
           ┌────────────── Born2FlapWind (Feld) ──────────────┐
           │  Base + Böen + Thermik + Hangaufwind              │
           ▼                                                    ▼
   StepMath (Luft-Relativ)                          Wind-Leaves (sichtbar)
   + b2f_math_set_wind_phase_noise                   fliegendes Laub + Gras
           │                                                    │
           ▼                                                    │
   B2F_FirmwareOutput ─► Telemetry ─► AeroAudio ─► Synth ─► PCM │
        (phase_error, k_gain, power, flaps)         (USynthComponent)
```

## Die drei Gesichter des Windes

1. **Physik** — `Born2FlapWind::Sample` liefert den Windvektor; der Flügel rechnet
   mit **Luft- statt Grundgeschwindigkeit** (`Velocity - Wind`), und
   `b2f_math_set_wind_phase_noise` speist die Turbulenz in die Phasen-Sperre des
   MathCore (die Resonanz, die der Servo halten muss).
2. **Klang** — `Born2FlapAeroAudio.h` ist das Live-Pendant zu `aero_audio.rb`:
   dieselbe pure Abbildung `Telemetry → 5 Stimmen`. Windböen brechen den
   Flügelton (`tone → 0`), Thermik erregt das Blätterrauschen, Last + Stall
   lassen die Servos ächzen.
3. **Sicht** — `ABorn2FlapWindLeaves` trägt Laub durch die Luft und biegt
   Grasbüschel; beides Instanzen der vorhandenen Gras-Meshes, angetrieben vom
   selben Feld.

## Neue Bausteine

| Datei | Rolle |
|---|---|
| `Audio/Born2FlapAudioSynth.{h,cpp}` | `USynthComponent` → `FAudioEngine` → PCM (thread-sicher) |
| `Audio/Born2FlapAeroAudio.h` | reine C++-Spiegelung von `aero_audio.rb` |
| `World/Born2FlapWind.{h,cpp}` | Wind-/Thermik-Feld (deterministisch, Perlin) |
| `World/Born2FlapWindLeaves.{h,cpp}` | sichtbarer Wind (Laub + Gras) |

## Verdrahtung

- `Born2FlapFlightPawn` besitzt den Synth, sampelt das Feld, rechnet Telemetrie +
  Kamera-Perspektive und speist pro Tick alle fünf Stimmen.
- `Born2FlapMathBridge` bekam `InjectWindPhaseNoise` (FFI `b2f_math_set_wind_phase_noise`).
- `Born2FlapGameMode` spawnt `ABorn2FlapWindLeaves` im Nature-Level.
- `Born2Flap.Build.cs` linkt `AudioMixer` (Modul von `USynthComponent`).

## Windows integration check - 2026-09-27

Merged upstream through `3f6ec8e`. The Windows build required replacing the
nonstandard `M_PI` macro with a portable constant in the shared synth header.
The live synth outputs stereo; listener position and pan follow the active
chase, ground or FPV camera. Ground view retains fly-by Doppler; a camera
travelling with the bird has no artificial bird-to-listener approach velocity.

Run `Unreal/Born2Flap/Tools/test-audio.ps1` after `build.ps1`. It starts the actual
rendered Ravenstonefield game with Windows audio enabled, hand-launches at
72% throttle, and checks samples produced by `USynthComponent` on the audio
render thread. It rejects absent/silent output, nonfinite samples and overflow.
It does not record racing ghosts or change personal control preferences.
This is signal validation, not a subjective listening assessment.

Flight regression modes disable wind and phase noise for reproducibility;
normal play and the audio integration test use the atmospheric field.

Verified on Windows / Unreal 5.8.2: 1,370,112 stereo samples, RMS 0.114733,
peak 0.532997, zero nonfinite samples. The final isolated run logged no audio
buffer underrun. The regenerated map's terrain/water/radio validation passed.

The bird samples wind with a smooth ground-shelter ramp over the first two
metres of clearance. This prevents the newly added free-stream wind from
rolling the stationary bird onto its side before launch. Aero-audio uses the
same sheltered airflow and free-flight wind resumes above that layer.
