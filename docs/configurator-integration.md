# Configurator-Integration

## Ausgangspunkt

Der vorhandene Configurator enthält bereits Anordnungen mit einem bis vier Flügelpaaren, Schwerpunkt und Montagepositionen, Schlagfrequenz und -amplitude, Servogeschwindigkeit, ONDAS-Modulation, Mixer, PID, Stick-Feedforward und Stabilisierungslogik.

Diese Logik ist fachlich wertvoll, liegt aber derzeit in einer UI-zentrierten CoffeeScript-Simulation. Sie wird nicht direkt in Unreal eingebettet, sondern über Tests spezifiziert und kontrolliert nach C++ portiert.

## Gemeinsames Format

Arbeitstitel: `OrniConfig`.

```json
{
  "schemaVersion": 1,
  "identity": {
    "id": "org.born2flap.example.tandem",
    "name": "Tandem Prototype"
  },
  "body": {
    "massKg": 1.2,
    "centerOfGravityM": [0.0, 0.0, -0.04],
    "inertiaKgM2": [0.03, 0.08, 0.09]
  },
  "wingPairs": [],
  "actuators": [],
  "controller": {},
  "appearance": {}
}
```

## Formatregeln

- SI-Einheiten und Einheit im Feldnamen
- stabile IDs statt UI-Anzeigenamen
- `schemaVersion` und dokumentierte Migrationen
- Trennung physikalischer, Controller- und kosmetischer Daten
- explizite Standardwerte
- Wertebereiche und verständliche Validierungsfehler
- unbekannte optionale Felder werden beim Lesen toleriert
- canonical JSON und Hash für Replays und Leaderboards

## Migrationsweg

1. aktuelle Simulatorparameter vollständig inventarisieren
2. Bedeutung, Einheit, Bereich und Default jedes Felds dokumentieren
3. JSON Schema erstellen
4. Export im Configurator ergänzen
5. Referenzdateien und Round-Trip-Tests erstellen
6. C++-Parser und Validator in OrniCore implementieren
7. Importdialog und Fehlerdarstellung in Unreal bauen

## Portierung der Steuerlogik

- zuerst Golden-Master-Tests im Configurator erzeugen
- feste Eingabesequenzen, Störungen und erwartete Servoausgaben speichern
- ONDAS, Mixer, PID und Slew Limits einzeln nach C++ portieren
- Ergebnisse tickweise mit Toleranz vergleichen
- erst danach physikalische Erweiterungen vornehmen

So bleiben Firmware-Nähe und neue Spielphysik voneinander unterscheidbar.
