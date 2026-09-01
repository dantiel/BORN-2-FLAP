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

## Segmentiertes dreidimensionales Flügelmodell

Jeder Flügel wird zunächst in 12 bis 24 Elemente entlang der Spannweite zerlegt. Diese Elemente sind keine voneinander isolierten zweidimensionalen Profile: Sie besitzen einen persistenten lokalen Strömungszustand und tauschen Zustandsgrößen mit ihren Nachbarn aus. Für jedes Element werden pro Simulationsschritt berechnet:

1. Welt- und Körperposition
2. Geschwindigkeit aus Körpertranslation und -rotation
3. Geschwindigkeit aus Schlag, Pitch, Sweep und elastischer Verformung
4. lokaler Wind und induzierte Geschwindigkeit
5. vollständige lokale Anströmung in Sehnen-, Normal- und Spannweitenrichtung
6. Anstell- und Schiebewinkel
7. Reynolds-Zahl
8. Ablösegrad, Crossflow und weitere instationäre Zustände
9. Lift, Drag und aerodynamisches Moment
10. Kraftangriffspunkt und Moment um den Schwerpunkt

Grundbeziehungen:

```text
q  = 0.5 * rho * |v_rel|^2
dL = q * chord * Cl(alpha, Re) * dr
dD = q * chord * Cd(alpha, Re) * dr
```

Koeffizienten werden aus dokumentierten Polaren oder kalibrierten Tabellen interpoliert. Außerhalb gesicherter Bereiche gelten begrenzte Extrapolationsmodelle.

## Partieller und dynamischer Strömungsabriss

Strömungsabriss ist kein globaler binärer Schalter. Jedes Flügelelement besitzt einen kontinuierlichen Ablösegrad `f`:

```text
f = 0.0  vollständig anliegende Strömung
f = 0.5  teilweise abgelöste Strömung
f = 1.0  weitgehend abgelöste Strömung
```

Die lokalen Koeffizienten werden zwischen anliegendem und abgelöstem Zustand überblendet:

```text
Cl = (1 - f) * Cl_attached + f * Cl_separated
```

Widerstand, aerodynamisches Moment und Druckmittelpunkt verändern sich ebenfalls mit dem Ablösegrad. Dadurch können Flügelwurzel, Mitte und Spitze gleichzeitig unterschiedliche Strömungszustände besitzen. Asymmetrischer partieller Stall erzeugt unmittelbar Roll-, Nick- und Giermomente.

Der Ablösegrad reagiert zeitverzögert auf Anstellwinkel, Reynolds-Zahl und Winkeländerungsrate:

```text
df/dt = (f_target(alpha, Re, d_alpha/dt) - f) / tau
```

Damit entstehen dynamischer Stall, verzögerte Ablösung, verzögerte Wiederanlegung und Hysterese zwischen Auf- und Abschlag. Diese Zustandsarchitektur gehört zum ersten Physikkern; ihre Koeffizienten werden schrittweise experimentell kalibriert.

## Spannweitiger Crossflow

Die lokale Relativgeschwindigkeit wird im Flügelrahmen zerlegt:

```text
v_rel = v_chord * e_chord + v_normal * e_normal + v_span * e_span
```

`v_span` beschreibt den spannweitigen Crossflow. Er wird nicht in einer zweidimensionalen Gesamtgeschwindigkeit verborgen, sondern als vorzeichenbehaftete Zustands- und Transportgröße geführt.

Positive und negative lokale Pfeilung erzeugen entgegengesetzte geometrische Crossflow-Tendenzen. Eine erste Modellform ist:

```text
v_cross = K_sweep * sin(sweep) * v_local
          * F(alpha, stroke_phase, stroke_rate, Re, separation)
```

Weil `sin(-sweep) = -sin(sweep)`, wechselt die Grundrichtung bei umgekehrter Pfeilung automatisch. Die tatsächliche lokale Richtung darf zusätzlich durch Schlagphase, Druckverteilung, Twist, Ablösung und Wake beeinflusst oder lokal umgekehrt werden. Die Vorzeichenkonvention wird vor der Implementierung anhand der Experimente eindeutig festgelegt.

Lokale Pfeilung wird aus der wirklichen Geometrie jedes Elements abgeleitet. Ein einzelner Flügel kann daher positive, neutrale und negative Pfeilungsbereiche enthalten. An Übergängen können Crossflow-Konvergenz, -Divergenz und Stauzonen entstehen.

## Transport entlang des Flügels

Crossflow transportiert Ablösung, Wirbelzustände und reduzierte Impulsgrößen zwischen benachbarten Elementen. Als Ausgangspunkt dient eine diskretisierte Transport-Diffusions-Gleichung:

```text
ds/dt + v_span * ds/dr = D * d2s/dr2 + sources - decay
```

Die Kopplung verhindert zugleich einen unphysikalisch fleckigen Stall, bei dem benachbarte Elemente ohne räumlichen Zusammenhang zwischen Zuständen springen. Eine diskrete Lifting-Line- oder vergleichbare reduzierte Wirbelkopplung soll später die dreidimensionale induzierte Geschwindigkeit ergänzen.

## Geplante instationäre Erweiterungen

Aufbauend auf partiellem Stall und Crossflow werden in dieser Reihenfolge und nur nach messbarem Bedarf ergänzt:

1. Added Mass
2. Rotationszirkulation
3. Leading-Edge-Vortex-Zustandsmodell
4. diskrete Lifting-Line oder einfache induzierte Geschwindigkeit
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

Die langjährigen Experimente und Strömungsbeobachtungen des Projektautors bilden die primäre fachliche Grundlage für Crossflow, partiellen Stall und stark instationäre Effekte. Validierung erfolgt in dieser Rangfolge:

1. systematisch dokumentierte eigene Experimente
2. neue reproduzierbare Strömungs- und Kraftversuche
3. eigener Kraft-/Momentenprüfstand und reale Fluglogs
4. analytische Grenzfälle, Symmetrien und numerische Konvergenz
5. veröffentlichte experimentelle Daten
6. PteraSoftware ausschließlich in geeigneten, überwiegend anliegenden Potentialströmungsfällen
7. höherwertige CFD für ausgewählte Einzelfälle

Jede Physikversion erhält einen Satz goldener Szenarien mit tolerierten Abweichungen.

## Genauigkeitsgrenzen

Das Modell darf keine Genauigkeit behaupten, die nicht validiert wurde. Besonders starke Ablösung, Leading-Edge-Vortices, clap-and-fling und komplexe Membranverformung brauchen empirische Korrekturen oder höherwertige Offline-Simulation.
