# Physikkonzept

## Ziel

Die Laufzeitphysik muss glaubwürdige, kontinuierliche Kräfte für unterschiedliche Flügelanordnungen liefern und auf einem normalen Spiele-PC ausreichend schnell für mehrere Ornithopter sein. Sie soll genauer als die derzeitige momentbasierte Configurator-Vorschau sein, aber bewusst keine vollständige CFD ersetzen.

## Starrkörper

Chaos integriert Translation, Rotation, Gravitation und Kollision. OrniCore liefert aerodynamische Kräfte und Momente sowie optional Gelenklasten. Benötigte Fahrzeugdaten:

- Masse und Trägheitstensor
- Schwerpunkt
- Rumpf- und Leitwerksgeometrie
- Position und Orientierung jedes Flügelgelenks
- Servodaten, Übersetzung und Bewegungsgrenzen

## Blade-Element-Modell

Jeder Flügel wird zunächst in 8 bis 16 radiale Elemente zerlegt. Für jedes Element werden pro Simulationsschritt berechnet:

1. Welt- und Körperposition
2. Geschwindigkeit aus Körpertranslation und -rotation
3. Geschwindigkeit aus Schlag, Pitch, Sweep und elastischer Verformung
4. lokaler Wind und induzierte Geschwindigkeit
5. Anstell- und Schiebewinkel
6. Reynolds-Zahl
7. Lift, Drag und aerodynamisches Moment
8. Kraftangriffspunkt und Moment um den Schwerpunkt

Grundbeziehungen:

```text
q  = 0.5 * rho * |v_rel|^2
dL = q * chord * Cl(alpha, Re) * dr
dD = q * chord * Cd(alpha, Re) * dr
```

Koeffizienten werden aus dokumentierten Polaren oder kalibrierten Tabellen interpoliert. Außerhalb gesicherter Bereiche gelten begrenzte Extrapolationsmodelle.

## Geplante instationäre Erweiterungen

In dieser Reihenfolge und nur nach messbarem Bedarf:

1. Added Mass
2. Rotationszirkulation
3. dynamischer Stall mit internem Zustandsmodell
4. einfache induzierte Geschwindigkeit
5. Interferenz zwischen Flügelpaaren
6. Wake-Capture-Korrektur
7. Ground Effect
8. aeroelastische Biege- und Torsionsmoden

## Servo- und Antriebsmodell

Ein idealer Winkelgenerator wäre zu optimistisch. Das Modell berücksichtigt schrittweise:

- maximale Winkelgeschwindigkeit und Beschleunigung
- Drehmomentgrenze
- Getriebeübersetzung und Wirkungsgrad
- Lastabhängige Verzögerung
- elektrische Leistung und Akkuspannung
- mechanische Anschläge und Schäden

Die gerenderte Flügelpose und die für Aerodynamik verwendete Pose müssen aus demselben Zustand stammen.

## Wind und Atmosphäre

Erste Version:

- Dichte, konstante Windkomponente und einfache Böen
- räumliche Windzonen
- deterministischer Turbulenzseed

Später:

- Gelände- und Gebäudeeinfluss
- Thermik
- korrelierte Turbulenzfelder
- Propwash- oder Wake-Zonen anderer Fahrzeuge

## Validierung

Validierung erfolgt stufenweise:

1. analytische Grenzfälle und Symmetrien
2. numerische Konvergenz über Zeitschritt und Elementzahl
3. Vergleich gegen PteraSoftware in geeigneten UVLM-Bereichen
4. Vergleich gegen veröffentlichte Versuchsdaten
5. eigener Kraft-/Momentenprüfstand
6. reale Fluglogs

Jede Physikversion erhält einen Satz goldener Szenarien mit tolerierten Abweichungen.

## Genauigkeitsgrenzen

Das Modell darf keine Genauigkeit behaupten, die nicht validiert wurde. Besonders starke Ablösung, Leading-Edge-Vortices, clap-and-fling und komplexe Membranverformung brauchen empirische Korrekturen oder höherwertige Offline-Simulation.
