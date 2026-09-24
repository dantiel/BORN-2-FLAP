# BORN-2-FLAP

> **EVERY WINGBEAT COUNTS.**

BORN-2-FLAP ist ein kostenloser, quelloffener Ornithopter-Flugsimulator auf Basis der Unreal Engine. Das Projekt verbindet Flugtraining, realistische Schlagfluegelphysik und zugängliche Spielmodi wie Rennen, Herausforderungen und freien Flug.

Konfigurationen aus dem OrniFlight Configurator sollen optional importiert werden können. Zusätzlich erhält das Spiel einen eigenen Hangar, in dem Ornithopter ausgewählt, verändert, getestet und kosmetisch personalisiert werden können. Der Configurator ergänzt das Spiel, ist aber keine Voraussetzung für das Spielerlebnis.

## Leitziele

- glaubwürdiges Fluggefühl mit nachvollziehbarer Echtzeitphysik
- Übernahme der ONDAS-, Servo-, Mixer-, PID- und Stabilisierungslogik des Configurators
- feste, von der Bildrate unabhängige Simulation
- Unterstützung realer RC-Sender sowie Gamepads und Tastatur
- Training und Spiel in derselben Simulationswelt
- offene Dateiformate, reproduzierbare Tests und öffentlich dokumentierte Physik
- grafische Qualität und Immersion in Richtung moderner FPV-Simulatoren

## Technische Richtung

- Unreal Engine 5.8 als zunächst festgelegte Produktionsbasis
- Chaos für Starrkörper, Kollisionen und Weltinteraktion
- Ruby als bewusstes Brain für App-Zustand, Menüs, Regeln, Plugins und Orchestrierung
- Haskell als kanonischer Mathematikkern für Aerodynamik, Kinematik, Steuerung und Stabilisierung
- C/C++ für Unreal, Chaos, Plattformanbindung, FFI und den vorläufigen getesteten Physik-Fallback
- segmentiertes dreidimensionales Flügelmodell mit dynamischem partiellem Stall und spannweitem Crossflow
- experimentelle Strömungsbeobachtungen des Projektautors als primäre Grundlage des reduzierten Echtzeitmodells
- PteraSoftware ausschließlich als sekundärer Offline-Vergleich in geeigneten Potentialströmungsfällen
- versioniertes JSON-Austauschformat zwischen Configurator, Werkzeugen und Spiel

## Dokumentation

- [Produktvision](docs/product-vision.md)
- [Systemarchitektur](docs/architecture.md)
- [Physikkonzept](docs/physics.md)
- [Experimentelles Aerodynamikmodell](docs/experimental-aerodynamics.md)
- [First-Flap-Prototyp](docs/first-flap-prototype.md)
- [Configurator-Integration](docs/configurator-integration.md)
- [PteraSoftware-Strategie](docs/pterasoftware.md)
- [Ornithopter-Editor](docs/ornithopter-editor.md)
- [Spielmodi und Training](docs/game-design.md)
- [Implementierungsplan](docs/implementation-plan.md)
- [Offene Entscheidungen](docs/open-questions.md)
- [ADR: Sprach- und Laufzeitarchitektur](docs/decisions/0001-language-and-runtime-architecture.md)
- [Windows-Entwicklungsumgebung](docs/windows-development.md)
- [Waldtal und direkter RC-Senderanschluss](docs/natural-valley-and-rc.md)

## Projektstatus

Der engine-unabhängige First-Flap-Prototyp ist implementiert. Er simuliert einen segmentierten Flügel mit festem 240-Hz-Zeitschritt, lokalem dynamischem Stall, vorzeichenbehaftetem Crossflow, Transport zwischen benachbarten Elementen und CSV-Telemetrie.

## First Flap bauen

Voraussetzungen sind ein C++20-Compiler und CMake ab Version 3.18.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
cmake -E chdir build ctest --output-on-failure
./build/Source/FirstFlap/first_flap first-flap.csv
```

Die CSV-Datei enthält pro Tick und Flügelelement unter anderem Anstellwinkel, Reynolds-Zahl, Crossflow, Ablösegrad, Lift und Drag. Das ausführbare Programm ist ein Forschungs- und Architekturprototyp; seine Koeffizienten sind noch nicht experimentell kalibriert.

## Ruby-Brain testen

```sh
ruby -I Brain/lib Brain/test/brain_test.rb
ruby -I Brain/lib Brain/bin/brain_demo
```

## Haskell-Math-Core testen

Mit GHC und Cabal:

```sh
cd MathCore
cabal test all
```

## Unreal-Prototyp

Das Unreal-5.8.2-Projekt liegt unter [`Unreal/Born2Flap`](Unreal/Born2Flap). Die Tastatur bewegt virtuelle RC-Knueppel: W gibt mittleres Gas, Shift+W Vollgas, A/D Seitenruder (Gieren), Pfeil links/rechts Querruder (Rollen) und Pfeil hoch/runter Hoehenruder. Kurzes Antippen ergibt kleine Ausschlaege; Halten faehrt die Knueppel langsam aus. Seitenruder veraendert die Schlagamplitude links/rechts, Querruder die asymmetrische Schlagzeit und Fluegelposition. Auch im Gleitflug reagieren die Fluegel. Alle Flugkraefte und -momente entstehen im Aerodynamikmodell. Space dient zum Handstart, R zum Reset. Koeffizienten und Antriebsdaten bleiben unkalibriert. Start und Builds: [Windows-Entwicklungsumgebung](docs/windows-development.md). Steuerung und Nachweise: [RC-Steuerung](docs/rc-controls.md).

## Lizenz

Der eigene Quellcode steht unter der [MIT-Lizenz](LICENSE). Unreal Engine und Drittanbieter-Assets behalten ihre jeweiligen separaten Lizenzen.