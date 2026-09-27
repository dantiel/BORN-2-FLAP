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
