# Implementierungsplan

## Leitstrategie

Zuerst wird ein vertikaler Ausschnitt gebaut, der die riskantesten Annahmen prüft: Fluggefühl, Performance, Config-Import und Regelungsport. Content-Menge, Multiplayer und ein umfangreicher Editor folgen erst danach.

## Phase 0: Projektfundament

Ergebnisse:

- Lizenz- und Contribution-Entscheidung
- Unreal-Version und unterstützte Plattformen festlegen
- Unreal-C++-Projekt und grundlegende Verzeichnisstruktur
- CI für OrniCore und Headless Tests
- Coding Style, Einheiten und Koordinatensystem dokumentieren
- minimale OrniConfig-v1-Spezifikation
- strukturierte Erfassung der vorhandenen experimentellen Strömungsbeobachtungen
- verbindliche Vorzeichenkonvention für positive/negative Pfeilung und Crossflow

Abschlusskriterium: Ein leerer Unreal-Build und OrniCore-Tests laufen reproduzierbar auf der primären Entwicklungsplattform.

## Phase 1: Controller-Parität

Ergebnisse:

- Configurator-Parameterinventar
- Golden-Master-Testsequenzen
- C++-Port von ONDAS, PID, Mixer und Servo-Slew
- Vergleichswerkzeug zwischen CoffeeScript und C++
- erste importierbare Tandem-Konfiguration

Abschlusskriterium: Die C++-Servoausgänge entsprechen für definierte Sequenzen dem Configurator innerhalb festgelegter Toleranzen.

## Phase 2: Physik-Minimum

Ergebnisse:

- 6-DOF-Verbindung zu Chaos
- Fixed-Step 240 Hz
- Blade Elements und grundlegende Polaren
- Flügelkinematik und Kräfte pro Element
- persistenter Ablösezustand pro Element und partieller dynamischer Stall
- dreidimensionale Geschwindigkeitszerlegung und vorzeichenbehafteter Crossflow
- Transportkopplung zwischen benachbarten Flügelelementen
- Rumpfwiderstand, Wind und Gravitation
- Kraft-, Moment- und Leistungs-Telemetrie
- analytische und numerische Tests

Abschlusskriterium: Ein Ornithopter kann starten, stabilisiert fliegen, gleiten, kurven und landen; das Ergebnis bleibt über unterschiedliche Render-Bildraten stabil.

## Phase 3: Vertical Slice

Ergebnisse:

- ein hochwertiges, überschaubares Fluggebiet
- ein vollständig animierter Ornithopter
- RC-Sender, Gamepad und Tastatur
- FPV- und Chase-Kamera
- eine kurze Fluglektion
- eine Race-Strecke mit Ghost
- Telemetrie und Replay
- schnelle Restart-Schleife

Abschlusskriterium: Externe Testpersonen können ohne Entwicklerhilfe lernen, eine Runde absolvieren und sinnvolles Feedback zum Fluggefühl geben.

## Phase 4: Experimentelle Kalibrierung und wissenschaftlicher Vergleich

Ergebnisse:

- formalisierte Regeln und Datensätze aus den eigenen Experimenten
- reproduzierbare Crossflow-, Stall- und Wiederanlegungsfälle
- OrniConfig-zu-PteraSoftware-Konverter für geeignete Vergleichsfälle
- standardisierte Parameter-Sweeps
- Vergleichsberichte OrniCore/Ptera
- Added Mass und Rotationszirkulation
- erste Flügelpaar-Interferenzkorrektur
- dokumentierte Gültigkeitsbereiche

Abschlusskriterium: Richtung, Transport und Wirkung von Crossflow sowie Stall- und Wiederanlegungsdynamik entsprechen den dokumentierten Experimenten innerhalb begründeter Toleranzen. Ptera-Vergleiche sind separat nach Gültigkeitsbereich bewertet.

## Phase 5: Hangar und Konstruktion

Ergebnisse:

- Basic- und Advanced-Editor
- Komponentenbibliothek
- Schwerpunkt- und Leistungsanalyse
- Prüfstand
- Undo/Redo und Variantenvergleich
- Import, Export und Teilen

Abschlusskriterium: Spieler können ohne Dateiänderung einen flugfähigen eigenen Entwurf erstellen und dessen Verhalten nachvollziehen.

## Phase 6: Spielausbau

- weitere Lektionen, Karten und Rennen
- Freestyle, Precision und Challenges
- Schäden und Reparaturregeln
- Ghost-Sharing und Leaderboards
- Barrierefreiheit und Lokalisierung
- Performance-Skalierung

## Phase 7: Reale Validierung und Multiplayer

- standardisierter Prüfstand
- Import realer Flight Logs
- System Identification und Koeffizienten-Fit
- synchrone Netzwerkrennen
- Anti-Cheat über Konfigurations- und Physikversionen

## Erste konkrete Tickets

1. Repository-Lizenz auswählen und `LICENSE` hinzufügen.
2. Unreal 5.7 installieren und C++-Projekt `Born2Flap` erzeugen.
3. `OrniCore` als engine-unabhängige CMake-Bibliothek anlegen.
4. Koordinaten- und Einheitentest erstellen.
5. aktuelle Simulatorfelder des Configurators inventarisieren.
6. OrniConfig JSON Schema v0.1 entwerfen.
7. Golden-Master-Exporter im Configurator planen.
8. ONDAS-Wellenform als erste reine C++-Komponente portieren.
9. Headless Servo-Trace gegen Configurator vergleichen.
10. Experimentkatalog für Pfeilung, Crossflow, Stall und Wiederanlegung anlegen.
11. einen einzelnen starren Flügel mit 12 bis 24 gekoppelten Elementen implementieren.
12. lokalen Ablösegrad und Crossflow-Zustand implementieren.
13. Kräfte und Strömungszustände im Unreal-Debug-View visualisieren.
14. Performancebudget auf Zielhardware messen.

## Definition of Done für Physikfeatures

Ein Physikfeature ist erst fertig, wenn es:

- eine dokumentierte Annahme und Einheit besitzt
- ohne Unreal getestet werden kann
- numerische Grenzfälle abdeckt
- Telemetrie zur Diagnose liefert
- sein Performancebudget einhält
- die Physikversion oder Replays nicht unkontrolliert bricht
