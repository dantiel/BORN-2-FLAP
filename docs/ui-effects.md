# UI-Effekte — Physik, die durchs Glas scheint (Kronjuwel)

Status: verbindliche Richtung · Entwurf

## Leitgedanke

Das polymorphe UI kann Effekte erzeugen, die strukturell **nur wir** haben, weil
die Dreischicht sie erst möglich macht: **Ruby besitzt den Zustand, Haskell kennt
die Physik, C++/Unreal malt die Pixel.** Der Reconciler propagiert diskrete
Zustandsänderungen als `:ui_patch`; auf einem **zweiten Kanal** propagiert der
`EffectDriver` kontinuierliche Physik-Telemetrie als **Materialparameter**.
Kein anderes Spiel hat eine Ruby-Reconciler→UMG-Brücke, die 60-Hz-Physik
(Flügelschlagfrequenz, Airspeed, AoA, Thermik) direkt in Slate-Materialien pumpt.

Das ist der Kronjuwel-Ansatz: **eine UI, die mit dem Vogel atmet.**

## Architektur: zwei Kanäle, ein Vertrag

React Native trennt diskrete Tree-Commits vom kontinuierlichen Animated-Pfad.
Genau diese Trennung fahren wir:

```text
Haskell (Physik)
      │  Telemetrie-Snapshot
      ▼
Effects (pure)  ────►  EffectDriver.drive  ────►  :ui_effect (EventBus)
                                                       │
                                                       ▼
                                         UMG-Host: UMaterialInstanceDynamic
                                                       │
                                                       ▼
                                         Slate-Material (Blur/Shimmer/Lensing)
```

- **`:ui_patch`** — diskrete Baum-Änderungen (`update_props`, `replace`, …).
- **`:ui_effect`** — kontinuierliche Materialparameter (`set_material_params`).
- **`set_material_params`** ist das **sechste Host-Op** (`HostConfig::HOST_OPS`),
  implementiert als `UMaterialInstanceDynamic::SetScalarParameterValue` im C++-Host.
  Es entsteht **nicht** aus einem Tree-Patch, sondern direkt aus Telemetrie.

Der `HostConfig` bleibt stabil — die Effekte sind nur *dynamische Props* auf
Nodes, wie `bind:` oder `onPress:` heute schon. Nichts am Reconciler ändert sich.

## Die Effekte (Telemetrie → Materialparameter)

Jeder Effekt ist eine **pure Funktion** `(telemetry) → { "Param" => Float }`,
headless-testbar ohne Unreal. Die Schlüssel sind wörtliche
`UMaterialInstanceDynamic`-Skalarnamen.

| Effekt | Treiber (Telemetrie) | Materialparameter |
|---|---|---|
| **Flap-Glow** | `wingbeat_hz × time` | `EmissiveIntensity`, `GlowRadius` — pulsiert im Flügelschlag |
| **Speed-Blur** (progressiv) | `airspeed` | `BlurRadius`, `BlurStrength` — schneller = weicher |
| **Thermal-Shimmer** | `thermal_strength` | `ShimmerAmplitude`, `ShimmerFrequency`, `ShimmerSpeed` |
| **Aero-Lensing** | `aoa_deg` | `RefractionIndex`, `Distortion` — Brechung wie Luft über dem Flügel |
| **Progressive-Blur** (Fokus) | `altitude − focus_depth` | `FocusDepth`, `BlurNear`, `BlurFar` — Tiefenschärfe |

## Progressive Blur — technisch

Unreal rendert das über ein **Post-Process-/Slate-Material**, das `SceneTexture`
mehrfach versetzt sampelt, gewichtet nach `BlurRadius`, moduliert durch eine
Maske. „Progressiv" heißt: der Radius variiert über den Screen **oder** über die
Zeit/den Zustand (hier: Airspeed bzw. Fokus-Offset). Der Blur ist nicht „ein
UI-Effekt von uns" — er ist der **Flügelschlag des Vogels, der durch die
Oberfläche sichtbar wird.** Kein Special Effect, sondern die Physik selbst.

## Bindungsmodell

```ruby
bus    = Born2Flap::EventBus.new
driver = Born2Flap::UI::EffectDriver.new(bus: bus)

tree = HamlParser.parse(<<~HAML)
  %Panel{ material: "FrostedGlass", effect: "speed_blur" }
  %Button{ material: "EmissiveHud", effect: "flap_glow" }
HAML

driver.bind_tree(tree)            # deklarativ: `effect:`-Attrs → Pfade binden
driver.drive(telemetry)           # → :ui_effect → set_material_params
```

- `material:` benennt das Basismaterial (→ dynamische Instanz im UMG-Host).
- `effect:` ist ein Ruby-seitiges Direktiv, das `bind_tree` in eine Pfad-Bindung
  übersetzt (Pfade identisch zur Reconciler-Konvention: Root = `[]`).
- `bind(effect, path:)` bindet auch imperativ.

## Implementierung (Brain-Tier)

```
Brain/lib/born2flap/ui/effects.rb        # Telemetry + pure Effekt-Funktionen
Brain/lib/born2flap/ui/effect_driver.rb  # Bindung + drive → :ui_effect
Brain/lib/born2flap/ui/host_config.rb    # + set_material_params (6. Host-Op)
```

Tests: `Brain/test/ui_effects_test.rb`. Demo: `Brain/bin/effects_demo`.
