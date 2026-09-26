# UI-Audio — die Stimme des großen Geistes

Die aero-audio-physik-engine verwandelt die aerodynamischen Kräfte des
Ornithopters in **prozeduralen Klang** — kein Sample, sondern Synthese, die mit
dem Vogel atmet. Sie ist der auditorische Zwilling der `set_material_params`
-Kronjuwelen (docs/ui-effects.md) und folgt exakt derselben Architektur:

```
Haskell (Physik + Resonanz) ──► Telemetry ──► AeroAudio (pure) ──► :audio_effect
                                                                        │
                                                       set_audio_params (NDJSON)
                                                                        │
                                              C++ FAudioEngine (Synthese) ──► PCM
```

## Die fünf Stimmen

| Stimme | Treiber | Klang |
|---|---|---|
| `wind` | Airspeed³ (Lighthill) | Breitbandrauschen, heller bei Tempo |
| `wing` | Flügelschlag + Resonanz δ | Tiefer Grundton + Downstroke-Whoosh |
| `servo_l/r` | Hinge-Load + Sweep-Rate | Grille (Chirp) ↔ genervtes Ächzen (Groan) |
| `leaves` | Airspeed + Thermik, niedrig | Blätterrauschen (nur über dem Kronendach) |

## Der entscheidende Kopplungspunkt: Resonanz

Der MathCore (`Born2Flap.Math.Resonance`) liefert **keine** strukturellen
Eigenmoden, sondern die **Phasen-Sperre** des Servos gegen Windphasenrauschen:
`phase_error` (δ), `k_gain_mod` (Phase-Advance-Demand) und die
Soft-Knee-Engagement. Genau diese Signale sind die „Stimme" der Schwingen:

- **δ → 0** (verriegelt): `wing.tone` → 1 — der Grundton singt sauber.
- **δ wächst** (Wind bricht die Sperre): `wing.tone` → 0 — der Atem wird ruppig.
- **Last → Stall**: `servo.groan` steigt, `servo.strain` härtet den Klang
  (Distortion), `groan_pitch` sinkt — das genervte Ächzen.

Das „erhabene, subtile Geräusch … wie ein Tier auf gewaltigen Samtpfoten" ist
der phasen-verriegelte Grundton (3–8 Hz) plus Subharmonische — tief, weich,
und durch `tone` an die Kohärenz des Flügelschlags gebunden.

## Perspektive

Jede Stimme wird uniform perspektivisch transformiert (in `AeroAudio.perspective`):

- **Distanz**: `gain ∝ 1/(1+(d/10)²)` (Inverse-Square-Annäherung).
- **Doppler**: `pitch = (c + v)/c`, c = 343 m/s (Annäherung → höher).
- **Pan**: `sin(bearing)` (Sine-Law, konstant-power im Host).
- **Luftabsorption**: `brightness` rollt mit der Distanz ab (Höhen dämpfen).

## Wire-Format

Neuer Host-Op (7. in `HostConfig::HOST_OPS`), parallel zu `set_material_params`,
aber per **Voice-Name** statt Baum-Pfad:

```json
{"op":"set_audio_params","voice":"servo_l","params":{"groan":0.92,"strain":0.88,"chirp":0.5,"pan":0.0,"pitch":1.0,"gain":0.7}}
```

`AudioDriver.drive(telemetry)` publiziert pro Tick **eine Op je Stimme** auf
`:audio_effect`; `Transport` sammelt den Kanal in denselben NDJSON-Frame.

## Die Schichten

| Schicht | Datei | Rolle |
|---|---|---|
| Physik | `Brain/lib/born2flap/audio/aero_audio.rb` | `Telemetry → voice params` (pure) |
| Treiber | `Brain/lib/born2flap/audio/audio_driver.rb` | publiziert `:audio_effect` |
| Synthese | `Unreal/.../UI/Born2FlapAudioEngine.h` | header-only, dependency-frei → PCM |
| Parser | `Unreal/.../UI/Born2FlapUiOps.h` | `set_audio_params` + `voice` |
| Host | `Unreal/.../UI/Born2FlapUIRenderer.{h,cpp}` | routed `SetAudioParams` → `FAudioEngine` |
| Beweis | `Tools/audio_render_test.cpp` | NDJSON → 10 s Stereo-WAV |

## Beweis (headless, ohne Engine)

`Brain/bin/audio_demo` erzählt einen 10-Sekunden-Flug (Start → Reiseflug →
Windböe → Strömungsabriss-Pull-up → Tiefflug überm Kronendach → Abflug) und
schreibt die NDJSON; `Tools/audio_render_test` rendert sie zu `flight.wav`.
Die RMS-Kurve **ist** die Geschichte:

```
  t=0-1s   -32.7 dBFS   Start (leise)
  t=1-4s   -24.5 dBFS   Reiseflug
  t=4-5s   -23.5 dBFS   Böe bricht Phasen-Sperre
  t=5-6s   -18.1 dBFS   Abriss — Servos ächzen am lautesten
  t=6-8s   -27.0 dBFS   Erholung + Blätterrauschen
  t=9-10s  -55.2 dBFS   Abflug — Distanz + Luftabsorption
```

## Offen

- **Echtzeit-Wiedergabe in Unreal**: `FAudioEngine` hält die Params; ein
  `USynthComponent::OnGenerateAudio` muss sie noch pro Sample rendern und in
  den Audio-Mixer speisen (Engine-Compile auf der Engine-Maschine).
- **Spatialisierung**: aktuell Sine-Law-Pan; HRTF/Binaural wäre der nächste
  Schritt für echte „vom Himmel gehörte" Richtungswahrnehmung.
