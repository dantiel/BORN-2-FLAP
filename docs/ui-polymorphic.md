# Polymorphes UI — UMGHAML (Königsweg)

Status: verbindliche Richtung · Entwurf

## Leitgedanke

Das UI ist **polymorph**: eine einzige, vom Ruby-Brain erzeugte **deklarative
UI-Beschreibung** (Baum) wird von **mehreren Renderern** dargestellt — In-Game
(Unreal Slate/UMG), Web (HTML/React), Mobile (React Native).

Polymorphismus hier wörtlich: *eine Schnittstelle, viele Implementierungen.*
Die Schnittstelle ist der UI-Baum (ein kleiner Satz Widget-Primitive), die
Implementierungen sind die Renderer.

## Der Königsweg: UMGHAML

**HAMLs Grammatik ist nicht HTML-gebunden.** `%Button`, `%Slider`, `%Overlay`,
Verschachtelung und Attribute sind eine **generische Baum-Notation**. HTML
entsteht erst im Emitter der Standard-Bibliothek (`:html5`). Wenn wir diesen
Emitter austauschen, erzeugt `%Button` kein `<button>` mehr — sondern ein
Unreal-Widget.

Deshalb führen wir **UMGHAML** ein: ein eigener, schlanker HAML-Subset-Parser
im Ruby-Brain (kein CoffeeHAML, keine Browser-Abhängigkeit). Er kompiliert
die HAML-Quelle in einen **neutralen Widget-Baum** statt in HTML.

```text
CoffeeHAML / UMGHAML (eine Grammatik)
        │  eigener Parser im Brain (kein HTML-Emitter!)
        ▼
neutraler Widget-Baum (Node-Tree · JSON-tauglich · mruby-freundlich)
        │
   ┌────┼────────────┐
   ▼    ▼            ▼
UMG/Slate   HTML/React   React Native
(In-Game)   (Web)        (Companion-App)
```

```haml
%Overlay{ id: "HUD" }
  %Slider{ bind: "throttle", min: 0, max: 100 }
  %Button{ onPress: "toggle_flap", class: "primary" }
    = "FLAP"
```

→ neutraler Baum (kein HTML):

```json
{ "type": "Overlay", "id": "HUD", "children": [
    { "type": "Slider", "bind": "throttle", "min": 0, "max": 100 },
    { "type": "Button", "onPress": "toggle_flap", "class": "primary",
      "children": [ { "type": "Text", "value": "FLAP" } ] } ] }
```

→ UMG mappt `type` → echte Widget-Klasse (`Overlay` → `UOverlay`,
`Slider` → `USlider`, `Button` → `UButton`). Dieselbe Quelle rendert im Web
über den HTML-Emitter, im Spiel über den UMG-Emitter, auf dem Telefon über
den React-Native-Emitter. **Ein Alphabet, drei Gesichter.**

## Warum kein CoffeeHAML im Kern

CoffeeHAML ist ein Web-Templating mit fest verdrahtetem HTML-Emitter. Im
Spielkern brauchen wir keinen Browser, also auch keinen HTML-Emitter.
UMGHAML trennt die Grammatik vom Emitter — es ist ein eigenes Modul, das
später als eigenständiges **Ruby-Gem `umghaml`** veröffentlicht wird.

## Live-Propagation: Reconciliation statt Redux

UMG/Slate haben **keine** React-artige Reconciliation — UMG ist zur Laufzeit
imperativ (`SetText`, `SetValue`), Slate bietet nur pull-basierte
`TAttribute`-Bindings. Die React-Leistung (Baum vergleichen, *nur das
Geänderte minimal patchen*) gehört deshalb in den Ruby-Brain — **idiomatisch
Ruby, ohne Redux**:

- **Reconciler**: `diff(previous, current) -> patch`. Vergleicht zwei neutrale
  Bäume wertbasiert und erzeugt eine minimale Operationsliste
  (`update_props`, `replace`, `insert_child`, `remove_child`).
- **Root**: hält den aktuellen Baum, `render(new_tree)` → reconciled und
  publiziert den Patch als `:ui_patch` auf den **EventBus** (der bestehende
  Brain-Kanal — keine neue Store-Infrastruktur).
- **Renderer**: abonnieren `:ui_patch` und wenden nur die Ops an
  (UMG → `SetText`/`SetValue`/`AddChild`, Web → DOM-Änderungen, RN → setState).

```ruby
bus  = Born2Flap::EventBus.new
root = UI::Root.new(bus: bus)
bus.subscribe(:ui_patch) { |e| apply(e[:patch]) }

root.render(tree_a)   # mount
root.render(tree_b)   # nur geänderte Slider-Props propagieren
root.render(tree_b)   # identisch → kein Patch
```

Kein Zustand lebt im Renderer; die Quelle der Wahrheit ist der Baum im Root.

## UI-Vertrag (neutrales Austauschformat)

1. **Widget-Primitive**: `overlay`, `panel`, `vbox`, `hbox`, `text`, `button`,
   `slider`, `progress`, `image`, `list`, `gauge`, `spacer`. Jedes mappt 1:1
   auf UMG, HTML und RN.
2. **Props**: Layout-Constraints, Stil, Datenbindungen, Event-Namen.
3. **Event-Roundtrip**: Renderer melden nur Namen an den EventBus zurück;
   der Brain reagiert. Kein Zustand lebt im Renderer.
4. **Reaktive Bindings**: Werte kommen aus dem Baum im Root, Updates als
   Diffs statt Voll-Neuaufbau.

## Taktrate

Das polymorphe UI ist event-getrieben (10–60 Hz, Ruby-Tier). Per-Frame-Gauges
(Speed, Altitude) sind **native** Widgets, direkt von Haskell-Telemetrie
gespeist — nicht der polymorphe Baum. Sonst fließt das Frame-Budget in
JSON-Serialisierung.

## Implementierung (Brain-Tier, headless testbar)

```
Brain/lib/born2flap/ui.rb           # Modul-Definition + requires
Brain/lib/born2flap/ui/node.rb      # neutraler Widget-Baum (Node)
Brain/lib/born2flap/ui/haml_parser.rb # UMGHAML → Node-Baum
Brain/lib/born2flap/ui/reconciler.rb # Baum-Diff → Patch (Reconciliation)
Brain/lib/born2flap/ui/root.rb      # reaktiver Render-Root (EventBus)
Brain/lib/born2flap/ui/emitter.rb   # UMG / HTML / RN Emitter
```

Tests laufen unter System-Ruby mit `minitest` (`Brain/test/ui_test.rb`).

## Zu vermeiden

- Eine JS-Engine (React Native) in Unreal einbetten.
- CoffeeHAML/HTML als Quelle der Wahrheit im Spielkern (die Quelle ist der
  neutrale Node-Baum; HTML ist nur ein Emitter).
- Zustand im Renderer halten (alles lebt im Baum/Root).