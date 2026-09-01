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
- quasi-stationäre Blade-Element-Methode mit gezielten instationären Korrekturen
- PteraSoftware als Offline-Referenzsolver und virtueller Windkanal
- versioniertes JSON-Austauschformat zwischen Configurator, Werkzeugen und Spiel

## Dokumentation

- [Produktvision](docs/product-vision.md)
- [Systemarchitektur](docs/architecture.md)
- [Physikkonzept](docs/physics.md)
- [Configurator-Integration](docs/configurator-integration.md)
- [PteraSoftware-Strategie](docs/pterasoftware.md)
- [Ornithopter-Editor](docs/ornithopter-editor.md)
- [Spielmodi und Training](docs/game-design.md)
- [Implementierungsplan](docs/implementation-plan.md)
- [Offene Entscheidungen](docs/open-questions.md)

## Projektstatus

Konzept- und Planungsphase. Das erste technische Ziel ist ein minimaler, testbarer Vertical Slice: ein importierter Ornithopter, ein Fluggebiet, RC-Eingabe, ONDAS-Regelung, eine erste Blade-Element-Physik und vollständige Telemetrie.

## Lizenz

Die endgültige Projektlizenz wird vor dem ersten Quellcode-Import festgelegt. MIT oder Apache-2.0 sind die bevorzugten Kandidaten. Unreal Engine und Drittanbieter-Assets behalten ihre jeweiligen separaten Lizenzen.
