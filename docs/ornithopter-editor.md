# Ornithopter-Editor

## Ziel

Der Hangar verbindet Konstruktion, Konfiguration und Personalisierung. Spieler sollen die physikalischen Auswirkungen einer Änderung sofort verstehen und den Entwurf anschließend auf Prüfstand oder Teststrecke ausprobieren können.

## Editierbare Bereiche

### Körper

- Rumpfform und Widerstandsparameter
- Masse, Schwerpunkt und Trägheit
- Komponentenpositionen
- Kollisionskörper

### Flügel

- Anzahl und Anordnung der Flügelpaare
- Spannweite, Sehne, Fläche und Profil
- Gelenkposition und Montagewinkel
- Schlag-, Pitch- und Sweep-Bereich
- Steifigkeit und einfache Verformungsparameter

### Antrieb

- Servo oder Mechanismus
- Geschwindigkeit, Drehmoment, Masse und Wirkungsgrad
- Übersetzung
- Akku und Energiegrenzen

### Regelung

- ONDAS-Profil
- Mixer
- PID
- Rates, Expo und Assists
- Sicherheitsgrenzen

### Erscheinung

- Materialien und Farben
- Decals
- optionale Verkleidungen
- Kamera und FPV-Ansicht

## UX-Prinzipien

- Basic-Modus mit sinnvollen Komponenten und Presets
- Advanced-Modus mit vollständigen Parametern
- Live-Anzeige von Masse, Schwerpunkt, Leistungsbedarf und Warnungen
- keine harte Sperre ungewöhnlicher Konstruktionen, solange sie numerisch sicher sind
- Prüfstand vor dem Flug
- Undo/Redo und Vergleich zweier Varianten
- Teilen über eine einzige versionierte Konfigurationsdatei

## Sicherheits- und Fairnessregeln

Rennen können Klassenregeln verwenden: Masse, Energie, Flügelfläche, Assist-Level und erlaubte Komponenten. Leaderboards speichern Konfigurationshash und Physikversion.
