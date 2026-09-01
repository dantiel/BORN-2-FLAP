# First-Flap-Prototyp

## Zweck

First Flap ist der erste ausführbare Grundstein von BORN-2-FLAP. Er prüft die Architektur des engine-unabhängigen Aerodynamikkerns, bevor Unreal, Chaos oder die Configurator-Steuerlogik angebunden werden.

Er ist ausdrücklich noch kein validierter Ornithopter-Simulator. Die aktuellen Koeffizienten sind transparente Ausgangswerte, die durch die dokumentierten Experimente ersetzt oder kalibriert werden.

## Implementierter Umfang

- C++20-Bibliothek `OrniCore` ohne externe Laufzeitabhängigkeiten
- SI-Einheiten und dokumentierter lokaler Flügelrahmen
- 12 bis 24 beziehungsweise frei konfigurierbare Elemente entlang der Spannweite
- lineare Variation von Sehne, Pfeilung und Twist
- sinusförmige Schlagkinematik mit festem 240-Hz-Schritt
- lokale Anströmung, Anstellwinkel und Reynolds-Zahl
- kontinuierlicher persistenter Ablösegrad pro Element
- unterschiedliche Zeitkonstanten für Ablösung und Wiederanlegung
- vorzeichenbehafteter Crossflow aus lokaler Pfeilung
- Upwind-Transport und Diffusion des Ablösezustands
- lokale Lift-, Drag-, Kraft-, Moment- und Leistungsschätzung
- CSV-Telemetrie pro Element und Tick
- versioniertes OrniConfig-0.1-JSON-Schema und Beispiel

## Tests

Die ersten automatischen Tests prüfen:

1. Positive und negative Pfeilung erzeugen entgegengesetzte Crossflow-Grundrichtungen.
2. Hoher lokaler Twist kann einen Teil des Flügels ablösen, während ein anderer Teil anliegend bleibt.
3. Langlaufzustände bleiben endlich und Ablösegrade bleiben in `[0, 1]`.

Diese Tests sichern Modellstruktur und numerische Grenzen, nicht die physikalische Kalibrierung.

## Beispielgeometrie

Der Beispiel-Flügel wechselt von `+24°` Pfeilung an der Wurzel zu `-8°` an der Spitze. Dadurch enthält derselbe Flügel eine Crossflow-Umkehrzone. Twist fällt von `19°` auf `7°`, sodass unterschiedliche lokale Stallzustände sichtbar werden.

## Aktuelle Grenzen

- ein einzelner Halbflügel statt eines vollständigen Fahrzeugs
- vorgeschriebene Kinematik statt Servo- und Lastkopplung
- keine 6-DOF-Starrkörperintegration
- keine induzierte Geschwindigkeit oder Lifting-Line-Kopplung
- noch kein Leading-Edge-Vortex-, Added-Mass- oder Wake-Capture-Modell
- Crossflow transportiert bislang einen reduzierten Ablösezustand, keine aufgelöste Strömung
- OrniConfig ist spezifiziert, aber noch nicht an einen JSON-Parser gebunden
- Koeffizienten sind noch nicht gegen die Experimente kalibriert

## Nächste technische Schritte

1. Experimentkatalog und eindeutige Vorzeichenkonvention anlegen.
2. Golden Cases für positive und negative Pfeilung erfassen.
3. OrniConfig-Parser ohne Unreal-Abhängigkeit ergänzen.
4. rechten und linken Flügel sowie Körperkräfte zu einem Fahrzeugzustand verbinden.
5. Servo- und ONDAS-Kinematik aus dem Configurator portieren.
6. OrniCore über einen schmalen Adapter an Chaos anbinden.
7. Kraftvektoren, Crossflow und Ablösegrad in Unreal visualisieren.

## Telemetriespalten

```text
time_s
stroke_deg
element
radius_m
sweep_deg
alpha_deg
reynolds
crossflow_m_s
separation
lift_n
drag_n
total_fx_n
total_fz_n
power_w
```

Jeder zukünftige Modellzustand muss diagnostizierbar bleiben. Verdeckte Korrekturkräfte ohne Telemetrie sind nicht vorgesehen.
