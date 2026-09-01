# PteraSoftware-Strategie

## Rolle

PteraSoftware ist ein wissenschaftlicher Offline-Referenzsolver, keine Laufzeitabhängigkeit des Spiels. Es stellt unter anderem steady VLM, unsteady ring VLM, Wake-Modelle, eine einfache Aeroelastik und gekoppelte freie 6-DOF-Simulation bereit.

## Wofür es eingesetzt wird

- Untersuchung von Flügel- und Wake-Interaktion
- Vergleich unterschiedlicher Flügelanordnungen
- Referenzdaten über Schlagzyklen
- Prüfung von Ground Effect und Formationseinflüssen
- Parameterstudien für Amplitude, Frequenz, Pitch und Fluggeschwindigkeit
- Kalibrierung reduzierter Echtzeitmodelle
- Visualisierung und Erklärung unsteady flow

## Wofür es nicht eingesetzt wird

- Physics Tick im ausgelieferten Spiel
- Kollisionen
- Multiplayer-Synchronisation
- Ersatz für Chaos
- uneingeschränkte Wahrheit bei Stall, starker Ablösung oder sehr kleinen Reynolds-Zahlen

## Integrationsform

Ein separates Python-Werkzeug liest OrniConfig und erzeugt PteraSoftware-Fälle. Resultate werden in ein neutrales Dataset exportiert:

```text
case metadata
time / stroke phase
body state
wing kinematics
per-wing force and moment
total force and moment
solver settings and convergence information
```

OrniCore und PteraSoftware werden anschließend mit denselben Kinematiken gespeist. Ein Vergleichswerkzeug berechnet Fehler, Phasenverschiebung und Korrekturfaktoren.

## Mögliche Übernahme von Code

PteraSoftware steht unter MIT. Klar abgegrenzte mathematische Hilfsfunktionen können nach Prüfung portiert werden, wenn dadurch ein tatsächlicher Vorteil entsteht. Jeder übernommene oder abgeleitete Abschnitt erhält Herkunft, Commit-ID und Lizenzhinweis.

Der UVLM-Solver wird zunächst nicht nach C++ portiert. Eine solche Portierung wäre ein eigenes Forschungsprojekt und kein notwendiger Schritt zum Vertical Slice.
