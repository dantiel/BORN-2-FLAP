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

- Unreal Engine 5.7 als zunächst festgelegte Produktionsbasis
- Chaos für Starrkörper, Kollisionen und Weltinteraktion
- eigener C++-Kern `OrniCore` für Aerodynamik, Kinematik und Flugregelung
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

## Lizenz

Der eigene Quellcode steht unter der [MIT-Lizenz](LICENSE). Unreal Engine und Drittanbieter-Assets behalten ihre jeweiligen separaten Lizenzen.
