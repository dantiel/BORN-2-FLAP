# ADR 0001: Sprach- und Laufzeitarchitektur

Status: angenommen

## Entscheidung

BORN-2-FLAP verwendet drei klar getrennte Verantwortungsebenen:

```text
Ruby Brain
    Produktzustand, Menüs, Regeln, Progression, Plugins, Orchestrierung
        ↓ Ereignisse und Kommandos
Haskell Math Core
    Aerodynamik, Kinematik, Steuerung, Stabilisierung, Zustandsübergänge
        ↓ stabile C ABI
C / Unreal C++
    Engine, Chaos, Rendering, Eingabeabfrage, Audio, Plattform und FFI
```

### Ruby

Ruby ist das bewusste Brain der Anwendung. Es entscheidet, was das Spiel tut, aber berechnet nicht den zeitkritischen Physikschritt.

Ruby verantwortet:

- App-Zustand und Menüführung
- Auswahl und Parametrisierung von Spielmodi
- Tutorials, Rennen, Challenges und Scoring
- Hangarabläufe und Konfigurationsverwaltung
- Savegames, Progression und Freischaltungen
- Pluginregistrierung und Capability-Prüfung
- Übersetzung nativer Ereignisse in Spielentscheidungen
- Anforderung und Aggregation von Telemetrie

Unreal rendert UMG/Slate. Ruby liefert Screen-Zustand, View-Model-Daten und Aktionen. Ruby zeichnet keine Widgets direkt und läuft nicht obligatorisch bei jedem Render- oder Physik-Tick.

Für die Einbettung ist mruby vorgesehen. Während der frühen Entwicklung darf dasselbe Brain mit System-Ruby headless getestet werden.

### Haskell

Haskell ist die kanonische mathematische Implementierung für:

- segmentierte Flügelaerodynamik
- Crossflow und Zustandstransport
- partiellen und dynamischen Stall
- Flügelkinematik
- Servo- und Aktuatormodelle
- ONDAS, Mixer, PID und Stabilisierung
- Fixed-Step-Zustandsübergänge
- Kraft-, Moment- und Leistungsberechnung

Die öffentliche Runtime-Grenze ist eine kleine C ABI. Unreal kennt keine Haskell-Datentypen. Der Haskell-Schritt verarbeitet mindestens einen vollständigen Flügel, bevorzugt das vollständige Fahrzeug, damit nicht pro Flügelelement FFI-Aufrufe entstehen.

Der Haskell-Pfad wird erst zum verbindlichen Runtime-Backend, wenn er die Golden Cases und das 240-Hz-Latenzbudget auf allen Zielplattformen erfüllt. Bis dahin bleibt der vorhandene C++-Kern Referenz und Fallback.

### C und Unreal C++

C/C++ enthält nur, was obligatorisch oder technisch vorzuziehen ist:

- Unreal-Module, Actors, Components und Subsystems
- Chaos-Rigid-Body und Kollision
- Eingabegeräte und Plattformabstraktion
- Kamera, Rendering, Animation, Audio und Debug Draw
- Fixed-Step-Orchestrierung
- C ABI und Speicherpuffer zu Haskell
- mruby-Einbettung und native Ruby-Capabilities
- deterministische Eventqueues zwischen den Laufzeitraten

Neue Spielregeln oder mathematische Modelle gehören nicht ohne begründeten Architecture Decision Record in C++.

## Laufzeitraten

```text
Haskell vehicle/control step   240 Hz oder höher nach Messung
Chaos integration              Unreal physics substeps
Ruby brain update              ereignisgetrieben, typischerweise 10–60 Hz
Ruby telemetry snapshots       standardmäßig 10–30 Hz
Rendering                      variabel
```

Ruby darf vollständige Diagnoseframes anfordern, ist aber kein Transportweg für jedes Element in jedem Physikschritt.

## Ereignisfluss

```text
Input device → Unreal C++ → Haskell command/control state
Haskell loads → C ABI → Unreal C++ → Chaos
Native events → bounded queue → Ruby Brain
Ruby commands → validated queue → Unreal/Haskell
Ruby view model → Unreal UMG/Slate
```

## Plugin-Sicherheit

Ruby-Plugins erhalten explizite Capabilities statt direkten Zugriffs auf beliebige Unreal-Objekte:

- `menu.read` und `menu.navigate`
- `race.events` und `race.rules`
- `vehicle.read`
- `vehicle.configure`
- `telemetry.read`
- `hud.publish`
- `storage.plugin`

Physikverändernde Plugins benötigen eine gesonderte Capability und markieren Session, Replay und Leaderboard als modifiziert. Plugins erhalten standardmäßig keinen freien Datei-, Netzwerk- oder Prozesszugriff.

## Konsequenzen

- Unreal bleibt auch ohne Ruby- oder Haskell-Bibliothek startfähig und zeigt einen klaren Backend-Fehlerzustand.
- Alle Grenzen verwenden versionierte, triviale Datenstrukturen und explizite Einheiten.
- Ruby- und Haskell-Logik muss außerhalb von Unreal testbar bleiben.
- Packaging ist komplexer als bei einem reinen C++-Projekt und wird früh in CI geprüft.
