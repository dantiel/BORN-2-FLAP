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

## State: Stores, Reducer, Dispatch (Redux-Philosophie)

Der UI-Zustand folgt dem Redux-Muster — eine einzige Quelle der Wahrheit:

- **Store**: hält den State, nimmt Actions entgegen, benachrichtigt Subscriber.
- **Reducer**: pure Funktionen `(state, action) -> state`. Keine Seiteneffekte.
- **Actions**: schlanke Hash-Objekte `{ type:, ... }`.
- **combine_reducers**: teilt den State in Teilmengen (`screen`, `hud`,
  `settings`, …) und setzt sie unverändert wieder zusammen, wenn nichts
  geändert wurde (Identitätsvergleich).
- **subscribe**: Renderer registrieren sich und erhalten State + Action; der
  Brain entscheidet, was neu gezeichnet wird. Kein Zustand lebt im Renderer.

```ruby
store = UI::Store.new(UI::Reducer.combine(screen: screen_reducer,
                                          hud:    hud_reducer))
store.dispatch(type: "UI_NAVIGATE", to: :free_flight)
store.dispatch(type: "HUD_BIND", bind: :throttle, value: 0.7)
```

## UI-Vertrag (neutrales Austauschformat)

1. **Widget-Primitive**: `overlay`, `panel`, `vbox`, `hbox`, `text`, `button`,
   `slider`, `progress`, `image`, `list`, `gauge`, `spacer`. Jedes mappt 1:1
   auf UMG, HTML und RN.
2. **Props**: Layout-Constraints, Stil, Datenbindungen, Event-Namen.
3. **Event-Roundtrip**: Renderer melden nur Namen an den EventBus zurück;
   der Brain reagiert. Kein Zustand lebt im Renderer.
4. **Reaktive Bindings**: Werte kommen aus dem Store, Updates als Diffs
   statt Voll-Neuaufbau.

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
Brain/lib/born2flap/ui/store.rb     # Store + Reducer.combine (Redux)
Brain/lib/born2flap/ui/emitter.rb   # UMG / HTML / RN Emitter
```

Tests laufen unter System-Ruby mit `minitest` (`Brain/test/ui_test.rb`).

## Zu vermeiden

- Eine JS-Engine (React Native) in Unreal einbetten.
- CoffeeHAML/HTML als Quelle der Wahrheit im Spielkern (die Quelle ist der
  neutrale Node-Baum; HTML ist nur ein Emitter).
- Zustand im Renderer halten (alles lebt im Store).
