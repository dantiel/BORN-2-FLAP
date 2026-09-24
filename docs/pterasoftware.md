# PteraSoftware-Strategie

## Rolle

PteraSoftware ist ein sekundärer wissenschaftlicher Offline-Vergleich, keine Laufzeitabhängigkeit und keine primäre Quelle des Strömungsmodells. Die langjährigen experimentellen Beobachtungen des Projektautors haben Vorrang, insbesondere bei Crossflow, partiellem Stall, Ablösung und stark instationären Schlagflügelzuständen.

PteraSoftware stellt unter anderem steady VLM, unsteady ring VLM, Wake-Modelle, eine einfache Aeroelastik und gekoppelte freie 6-DOF-Simulation bereit. Diese Methoden sind vor allem für geeignete Potentialströmungsfälle und induzierte dreidimensionale Auftriebsverteilungen nützlich.

## Wofür es eingesetzt wird

- Untersuchung von Flügel- und Wake-Interaktion
- Vergleich unterschiedlicher Flügelanordnungen
- Referenzdaten über Schlagzyklen
- Prüfung von Ground Effect und Formationseinflüssen
- Parameterstudien für Amplitude, Frequenz, Pitch und Fluggeschwindigkeit
- Plausibilitätsvergleich reduzierter Echtzeitmodelle innerhalb seiner Gültigkeitsbereiche
- Visualisierung und Erklärung unsteady flow

## Wofür es nicht eingesetzt wird

- Physics Tick im ausgelieferten Spiel
- Kollisionen
- Multiplayer-Synchronisation
- Ersatz für Chaos
- uneingeschränkte Wahrheit bei Stall, starker Ablösung oder sehr kleinen Reynolds-Zahlen
- Autorität über experimentell beobachteten Crossflow oder dessen Umkehr bei positiver und negativer Pfeilung
- Ersatz für eigene Versuche, Messdaten oder höherwertige CFD

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

OrniCore und PteraSoftware werden anschließend mit denselben Kinematiken gespeist. Ein Vergleichswerkzeug berechnet Differenzen und Phasenverschiebungen. Abweichungen werden nicht automatisch als Fehler in OrniCore behandelt, sondern gegen Gültigkeitsbereich und experimentelle Evidenz geprüft.

## Lizenz und Eigenständigkeit

BORN2FLAP verwendet ein vollständig eigenständig entwickeltes Strömungs- und Aeroelastikmodell. PteraSoftware dient ausschließlich der Analyse und Datenverifikation: Es wird als externer wissenschaftlicher Vergleich herangezogen, um Referenzdaten zu erzeugen und Plausibilitätsprüfungen durchzuführen. Eine Übernahme von Code findet nicht statt.