# Offene Entscheidungen

## Vor dem ersten Code

- Projektlizenz: MIT oder Apache-2.0?
- primäre Plattform: Windows zuerst oder Windows und macOS gleichzeitig?
- Mindesthardware und Zielbildrate
- exakte Unreal-Version: 5.7.x fest pinnen
- bleiben proprietäre Marketplace-Assets vollständig optional?

## Physik

- typische Größen-, Massen- und Reynolds-Bereiche der ersten Fahrzeuge
- starre Flügel als Ausgangspunkt oder sofort eine Torsionsmode?
- verfügbare Airfoil-Polaren bei niedriger Reynolds-Zahl
- erforderliche maximale Flügelpaarzahl
- Regler- und Aerodynamikraten
- Zielgenauigkeit gegenüber Messdaten
- welche realen Prüfstandsdaten können früh erzeugt werden?

## Configurator

- welche Parameter sind Firmware-exakt und welche nur Simulatorparameter?
- soll der Configurator OrniConfig vollständig bearbeiten oder nur Controllerdaten exportieren?
- wie werden alte Konfigurationen migriert?
- gemeinsame Bibliothek oder bewusst getrennte Implementierungen mit Golden Tests?

## Produkt

- zunächst Desktop-only?
- welche Senderprotokolle und Geräte sind prioritär?
- realistische Schäden im ersten öffentlichen Release?
- Workshop/Modding über Dateien oder eingebettete Werkzeuge?
- Umgang mit Ranglisten bei frei editierbaren Konstruktionen

## Forschung und Daten

- welche PteraSoftware-Fälle entsprechen unseren Flugregimen?
- welche Effekte liegen außerhalb der UVLM-Gültigkeit?
- dürfen veröffentlichte Messdaten redistributioniert werden?
- Format und Lizenz eigener Kalibrierungsdatasets
