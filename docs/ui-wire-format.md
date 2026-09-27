# UI-Wire-Format — die Ruby↔UMG-Brücke

Der Ruby-Brain läuft **standalone** (kein mruby-Embedding in Unreal). Deshalb
braucht es ein serialisiertes Drahtformat, über das die Op-Streams die
Prozessgrenze überqueren. `Brain/lib/born2flap/ui/wire.rb` ist der einzige
Serializer; die C++-Seite parst dasselbe Format.

## Zwei Kanäle, ein Schema

| Kanal | Erzeuger | Inhalt |
|---|---|---|
| `:ui_patch` | `HostConfig.translate(patch)` | diskrete Baum-Commits (5 Ops) |
| `:ui_effect` | `EffectDriver.drive(telemetry)` | kontinuierliche Material-Params |

Beides ist ein JSON-**Array von Op-Objekten**:

```json
[
  {"op":"create_instance","path":[],"node":{"type":"Overlay","props":{"id":"HUD"},"children":[]}},
  {"op":"remove_instance","path":[0]},
  {"op":"append_child","path":[0],"index":1,"node":{"type":"TextBlock","props":{},"children":[]}},
  {"op":"remove_child","path":[0],"index":1},
  {"op":"update_props","path":[0,0],"props":{"value":"BORN-2-FLAP II"}},
  {"op":"set_material_params","path":[0],"params":{"BlurRadius":0.6}}
]
```

## Konventionen

- **`path`** = Array von Kind-Indizes ab Wurzel; die Wurzel selbst ist `[]`.
- **`node`** = neutraler Teilbaum `{"type": UMGName, "props": {...}, "children":[...]}`.
- `type` wird via `Emitter.umg_type` gemappt (z.B. `:vbox` → `"VerticalBox"`).
- `props`/`params` sind skalare Werte (String/Number/Bool/Null); Arrays werden
  vom C++-Parser als `FValue::Array` erkannt, aber der v1-Host nutzt sie nicht.
- `index` erscheint nur bei `append_child`/`remove_child`.

## C++-Seite (kein Engine-Bedarf für Kern + Parser)

| Datei | Rolle | kompilierbar ohne Engine |
|---|---|---|
| `UI/Born2FlapUiOps.h` | `FValue`/`FOp`-Modell + dependency-freier JSON-Parser (`ParseOps`) | ja |
| `UI/Born2FlapUiTree.h` | `FRenderer<Host>` — host-agnostische Baum-Mutation (Mount/Unmount/Append/Remove/Update) | ja |
| `UI/Born2FlapUIRenderer.{h,cpp}` | der UMG-Host: `UWidget`-Fabrik, Prop-Mapping, `SetScalarParameterValue` | **nein** (UE 5.7) |

Der Kern ist bewusst header-only und dependency-frei, damit **dieselbe**
`FRenderer`-Logik in Unreal *und* im Headless-Test läuft.

## Brücke (noch offen)

Der Transport selbst ist bewusst austauschbar und **noch nicht gebaut**:
Pipe/Datei/Socket (pragmatisch) oder mruby-Embedding (Architektur-Ziel, ADR-0001).
Der C++-Host kennt nur `ApplyOpsJson(FString)` — woher der String kommt, ist
seine Sache nicht.

## Verifikation

- Ruby: `test/ui_wire_test.rb` (3 Tests) — Serializer round-trip.
- C++ headless: `Tools/umg_host_test.cpp` — parst den **echten** Ruby-Byte-Stream
  und prüft Baum-Aufbau, `update_props`, `set_material_params`, `remove_child`,
  `append_child`, `remove_instance` gegen einen Fake-Host.
