# UI-Transport — die Prozessgrenze (NDJSON-Bridge)

Die fehlende Brücke zwischen dem standalone Ruby-Brain und dem C++/UMG-Host.
Der Brain läuft **ohne mruby** in einem eigenen Prozess; der Transport ist das
Einzige, was über die Prozessgrenze geht.

## Framing: newline-delimited JSON (NDJSON)

Eine Zeile = ein Frame = ein komplettes `Wire.dump_ops`-Array. Ein Frame pro
Brain-Tick. Der Reader braucht keine Frame-Grenzen außer `\n` — eine kaputte
oder halbe Zeile wird übersprungen, der nächste Zeilenumbruch re-synchronisiert.

```
{"op":"create_instance","path":[],"node":{...}}\n
{"op":"update_props","path":[0,0],"props":{"value":"BORN-2-FLAP II"}}\n
{"op":"set_material_params","path":[0,1],"params":{"BlurRadius":1.0}}\n
```

## Datenfluss

```
Brain-Tick:
  state → Root.render(tree)         → :ui_patch   ┐
  telemetry → EffectDriver.drive(t) → :ui_effect  ┴→ Transport.flush → 1 NDJSON-Frame

C++/UMG-Tick:
  FTransport::Poll(renderer) → je Zeile ParseOps → renderer.Apply(ops)
```

## Die beiden Hälften

| Seite | Datei | Rolle |
|---|---|---|
| Writer | `Brain/lib/born2flap/ui/transport.rb` | sammelt `:ui_patch` + `:ui_effect`, schreibt 1 Frame/Tick auf beliebigen IO (stdout/Datei/Socket) |
| Reader | `Unreal/.../UI/Born2FlapTransport.h` | liest `std::istream` zeilenweise, `ParseOps` + `FRenderer::Apply` je Frame |

Der Reader ist **header-only und dependency-frei**: dieselbe Logik läuft im
UMG-Host (über das vorhandene `ApplyOpsJson`) und headless in
`Tools/umg_transport_test.cpp` (plain clang++).

## Pfad-Ausrichtung (wichtig)

`Root.render` und `EffectDriver.bind_tree` müssen **denselben** Baum bekommen —
das Top-Widget (`HamlParser.parse(src).children.first`), **nicht** den
Dokument-Root. Sonst verschieben sich die Effekt-Pfade um eins gegenüber den
Patch-Pfaden. Konvention: Root selbst = `[]`.

## End-to-end Beweis

```sh
ruby Brain/bin/transport_demo > /tmp/ops.ndjson
clang++ -std=c++17 -I Unreal/Born2Flap/Source/Born2Flap/UI \
        Tools/umg_transport_test.cpp -o Tools/umg_transport_test
./Tools/umg_transport_test /tmp/ops.ndjson
# → 6 frame(s) applied · all assertions passed
```

## Offen (Engine-seitig)

Der UMG-Host ruft im Tick `FTransport`-Frames über `ApplyOpsJson` auf — der
eigentliche Unreal-Compile steht noch aus (Engine auf dieser Maschine nicht
installiert). Transportmedium (Pipe/Datei/Socket) ist durch `std::istream`/IO
bewusst abstrahiert.
