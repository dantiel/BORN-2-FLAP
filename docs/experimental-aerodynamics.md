# Experimentelles Aerodynamikmodell

## Zweck

Dieses Dokument definiert, wie die über mehrere Jahre experimentell gewonnenen Erkenntnisse des Projektautors in ein nachvollziehbares und testbares Echtzeitmodell überführt werden. Diese Beobachtungen sind eine primäre Wissensquelle von BORN-2-FLAP und haben bei Crossflow, partieller Ablösung und stark instationären Schlagflügelzuständen Vorrang vor nicht passend validierten Standard- oder Potentialströmungsmodellen.

## Grundannahmen

- Der Flügel ist ein zusammenhängendes dreidimensionales Strömungssystem, keine Sammlung unabhängiger 2D-Profile.
- Strömungszustände unterscheiden sich gleichzeitig entlang der Spannweite.
- Partieller Stall ist kontinuierlich, räumlich gekoppelt und zeitabhängig.
- Crossflow transportiert Strömungszustände entlang der Spannweite.
- Positive und negative lokale Pfeilung erzeugen entgegengesetzte Crossflow-Grundtendenzen.
- Schlagphase, Schlaggeschwindigkeit, Pitch, Twist, Druckverteilung, Ablösung und Wake können Stärke und lokale Richtung verändern.
- Flügelform und lokale Geometrie müssen elementweise berücksichtigt werden.

## Erfassung eines experimentellen Phänomens

Jede Beobachtung soll möglichst mit folgenden Angaben dokumentiert werden:

1. eindeutige Experiment-ID und Datum
2. Versuchsaufbau und Mess- oder Visualisierungsmethode
3. Flügelgrundriss, Profil, Material und Flexibilität
4. lokale Pfeilung, Sehne und Verwindung entlang der Spannweite
5. Anströmung, Reynolds-Bereich und Umgebungsbedingungen
6. Schlagamplitude, Frequenz, Phase, Pitch und Sweep
7. Position und Richtung des beobachteten Crossflows
8. anliegende, teilweise oder weitgehend abgelöste Strömung
9. zeitlicher Verlauf, Hysterese und Wiederanlegung
10. qualitative oder quantitative Wirkung auf Kräfte und Momente
11. Video, Bilder, Rohdaten und Auswertung
12. Sicherheit der Interpretation und bekannte Alternativerklärungen

## Phänomenmatrix

Die Beobachtungen werden zunächst ohne erzwungene Gleichung in einer Matrix gesammelt:

| Geometrie | Zustand | Crossflow | Transport | Wirkung | Evidenz |
|---|---|---|---|---|---|
| lokale positive Pfeilung | zu definieren | vorzeichenbehaftete Richtung A | zu definieren | zu definieren | Experiment-ID |
| lokale negative Pfeilung | zu definieren | entgegengesetzte Richtung B | zu definieren | zu definieren | Experiment-ID |
| Pfeilungsübergang | zu definieren | Konvergenz oder Divergenz | zu definieren | zu definieren | Experiment-ID |

Die konkreten Richtungsnamen werden erst nach Festlegung des Flügelkoordinatensystems eingetragen. Dadurch vermeiden wir Missverständnisse durch Begriffe wie wurzelwärts, spitzenwärts, vorwärts oder rückwärts.

## Reduzierte Zustandsgrößen

Ein Flügelelement soll mindestens folgende aerodynamische Zustände führen:

```text
local angle of attack
local Reynolds number
signed spanwise flow velocity
separation fraction
separation transport flux
induced velocity
optional leading-edge-vortex strength
confidence / calibration regime identifier
```

## Übersetzung in Software

Für jede experimentelle Regel entstehen:

1. eine textuelle Aussage mit Gültigkeitsbereich
2. eine mathematische oder tabellarische Modellform
3. ein reproduzierbarer Testfall
4. erwartete Richtungen und qualitative Trends
5. nach Messung quantitative Toleranzen
6. Telemetrie- und Debugdarstellung

Eine Regel wird nicht allein deshalb übernommen, weil sie in einem Referenzsolver erscheint. Ebenso wird eine experimentelle Beobachtung nicht vorschnell verallgemeinert: Gültigkeitsbereich und Evidenz bleiben Teil des Modells.

## Modellhierarchie

```text
eigene dokumentierte Experimente
          ↓
reduzierte physikalische Zustandsregeln
          ↓
OrniCore Echtzeitsimulation
          ↓
Kraftmessung und reale Fluglogs

PteraSoftware / veröffentlichte Modelle / CFD
          ↓
Vergleich, Ergänzung und Widerspruchsanalyse
```

## Offene Arbeit

- Flügelkoordinatensystem und Pfeilungsvorzeichen endgültig definieren
- vorhandene Experimente inventarisieren
- Videos, Skizzen und Rohdaten persistent archivieren
- erste Crossflow-Phänomenmatrix gemeinsam ausfüllen
- einen positiven und einen negativen Pfeilungsfall als Golden Cases auswählen
- beobachtbare Größen für neue Kraft- und Strömungsversuche festlegen
