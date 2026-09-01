# Systemarchitektur

## Übersicht

```text
OrniFlight Configurator -----> OrniConfig JSON <----- In-Game Hangar
                                      |
                                      v
                               Config Validator
                                      |
                    +-----------------+-----------------+
                    |                                   |
                    v                                   v
             OrniCore Runtime                    Offline Toolchain
     Kinematics | Aero | Control            PteraSoftware | Fitting
                    |                                   |
                    v                                   v
             Unreal / Chaos <---------- Calibration datasets
       World | Collision | Rendering
```

## Module

### OrniCore

Engine-unabhängige C++-Bibliothek mit möglichst wenigen Abhängigkeiten:

- Einheiten, Vektoren und Koordinatensysteme
- Ornithopterdefinition und Validierung
- Servo- und Flügelkinematik
- ONDAS, Mixer, PID und Stabilisierung
- Blade-Element-Aerodynamik
- Windabfrage über eine kleine Schnittstelle
- Kräfte, Momente, Energie und Telemetrie
- deterministischer Fixed-Step-Simulationszustand

OrniCore soll ohne Unreal in Unit Tests, Kommandozeilenwerkzeugen und Benchmarks ausführbar sein.

### Unreal-Integration

- Übertragung von Eingaben an OrniCore
- Fixed-Step/Substep-Orchestrierung
- Anwendung von Kräften und Momenten auf Chaos
- Skeletal-Mesh- und Flügelanimation aus demselben Kinematikzustand
- Kollision, Schäden, Kamera, Audio und Effekte
- Benutzeroberfläche, Hangar und Spielmodi
- Replay, Ghosts und spätere Netzwerkfunktionen

### Offline-Aerodynamik

- Import derselben OrniConfig-Dateien
- Erzeugung von PteraSoftware-Studien
- Parameter-Sweeps und Konvergenzanalysen
- Export von Referenzkräften und Korrekturtabellen
- Vergleich mit Prüfstand und realen Fluglogs

## Zeitmodell

- Rendering: variabel, typischerweise 60 bis 120 Hz
- Gameplay: Unreal-abhängig
- OrniCore: fester Zeitschritt, zunächst 240 Hz
- Flugregler: eigene konfigurierbare Rate, gegebenenfalls mehrere Regler-Ticks pro Aero-Tick
- Telemetrie: entkoppelt und heruntergesampelt

Eine Bildratenänderung darf Flugbahn und Reglerverhalten nicht wesentlich verändern.

## Koordinaten und Einheiten

Innerhalb von OrniCore und Dateien werden ausschließlich SI-Einheiten verwendet. Die Unreal-Adaptergrenze konvertiert Meter in Zentimeter und bildet Achsen explizit ab. Jede öffentliche Struktur dokumentiert Referenzrahmen und Vorzeichen.

## Determinismus und Replays

Ein Replay speichert mindestens Konfigurationshash, Physikversion, Eingaben, Anfangszustand und Zufallsseed. Vollständiger Plattformdeterminismus ist kein Muss für den ersten Prototyp; reproduzierbare lokale Regressionen sind es.
