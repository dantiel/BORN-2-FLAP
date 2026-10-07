# Expansionspläne

> **EVERY WINGBEAT COUNTS.** — vom Simulator zur Plattform.

BORN-2-FLAP beginnt als kostenloser, quelloffener Ornithopter-Flugsimulator.
Die Expansion macht daraus eine offene Konstruktions- und Verteilungsplattform
für Fluggeräte jeder Art: vom naturgetreuen Vogel über das mehrflüglige Insekt
bis zum gewagten Flugapparat, den es in der Natur nie gab.

Drei Säulen, ein gemeinsamer Kern:

1. **BORN-2-FLAP (Simulator)** — bleibt kostenlos und quelloffen. Der Einstieg,
   die Referenzwelt, das Spielfeld.
2. **BORN2FLAP-STUDIO** — das bezahlte Konstruktionswerkzeug (einmalig
   ~100 EUR, kein Abo). Damit baut jeder Künstler, Maker und Ingenieur eigene
   Ornithopter inklusive Texturen und Vektor-Editor.
3. **BORN2FLAP MARKETPLACE** — der Marktplatz, auf dem Creators ihre Designs
   teilen, verkaufen und lizenzieren.

Alle drei sprechen dieselbe Sprache: das versionierte OrniConfig-Format, den
gemeinsamen Aerodynamik- und Stabilisierungskern (OrniCore/ONDAS) und dieselben
Klassenregeln. Was im Studio gebaut wird, fliegt im Simulator identisch — und
umgekehrt. Der Configurator-Import bleibt eine optionale Brücke, nicht das
Zentrum.

## Die große Linie

Die Expansion verändert nicht die Physik, sondern die Reichweite. Der
Simulator bleibt die Wahrheitsinstanz: Jede Konstruktion wird gegen dieselbe
Echtzeit-Aerodynamik geprüft, jede Änderung hat dieselben Konsequenzen. Studio
und Marketplace erweitern lediglich, *was* konstruiert, *wie* es gestaltet und
*an wen* es verteilt wird.

Die Produktprinzipien aus der [Produktvision](product-vision.md) gelten
unverändert:

- **Physikalisch erklärbar** — keine versteckten Stabilitätskräfte, Assists als
  sichtbare Regler.
- **Konstruktion hat Konsequenzen** — Masse, Schwerpunkt, Flügelsteifigkeit,
  Servo und Kinematik verändern das Verhalten messbar.
- **Offen und reproduzierbar** — dokumentierte Formate, Konfigurationshash,
  Physikversion, Regressionstests.

## Säule I — BORN2FLAP-STUDIO

### Positionierung

Der [Ornithopter-Editor im Spiel](ornithopter-editor.md) (der Hangar) bleibt der
kostenlose Basic-Modus. Das Studio ist der vollwertige Advanced-Modus als
eigenständiges Werkzeug:

| | Hangar (im Spiel) | Studio (~100 EUR) |
|---|---|---|
| Rolle | Spieler-Personalisierung | vollständige Konstruktion |
| Flügel | vorgegebene Grundtypen | beliebige Anzahl und Anordnung |
| Erscheinung | Materialien, Decals | Texturen, Vektor-Editor, UV |
| Export | OrniConfig teilen | alle Formate, Fertigung, Doku |
| Physik | gleicher Kern | gleicher Kern + erweiterte Analyse |

Beide teilen denselben Kern: dieselbe Physik, dasselbe Dateiformat, dieselben
Klassenregeln. Das Studio ist keine getrennte Welt, sondern das vergrößerte
Fenster in dieselbe.

### Konstruktion

- **Körper** — Rumpfform, Masse, Schwerpunkt, Trägheit, Komponentenpositionen,
  Kollisionskörper.
- **Flügel** — von einem Paar bis zu beliebiger Mehrflügeligkeit: Vogelflügel,
  Membranflügel, Tandems, zwei- und vierflüglige Insekten, mehrstöckige und
  versetzte Anordnungen. Spannweite, Sehne, Fläche, Profil, Gelenkposition und
  Montagewinkel je Servo, Schlag-/Pitch-/Sweep-Bereich, Steifigkeit und
  Verformung.
- **Antrieb** — Servos, Mechanismen, Getriebe, kinematische Aktuatoren als
  Muskel-Analogie, Akku und Energiegrenzen.
- **Regelung** — ONDAS-Profil, Mixer, PID, Rates, Expo, Assists,
  Sicherheitsgrenzen.
- **Gewagte Flugapparate** — Konstruktionen jenseits der Natur sind erlaubt,
  solange sie numerisch stabil bleiben. Keine harte Sperre, sondern Warnungen
  und ein Prüfstand vor dem Flug.

### Erscheinung, Texturen und Vektor-Editor

Das Studio trägt einen vollwertigen Grafik-Arbeitsplatz in sich:

- **Vektor-basierter Livery-Editor** (SVG-artig) für Flügelmembranen, Decals,
  Markierungen und Beschriftungen — auflösungsunabhängig und direkt auf die
  Flügelgeometrie projiziert.
- **Mehrschichtiges System** — Verläufe, Masken, Verlaufsgitter, Wiederholungs-
  und Spiegelungsoptionen entlang der Flügelachse.
- **Materialsystem** — PBR mit Basis, Normal, Rauheit, Transluzenz und
  emissivem Schimmer, passend zur bestehenden Material-Architektur.
- **Prozedurale Texturen** — Rauschen, Federnmuster, Membranadern als
  nicht-destruktive Ebenen.
- **Live-3D-Vorschau** — Beleuchtung, Bewegung der Flügel, Kamerafahrt;
  Änderungen erscheinen sofort auf dem fliegenden Modell.

### Export in alle Formate

Der Export ist die Brücke zwischen virtuell und physisch:

- **Austausch** — OrniConfig (versioniertes JSON), glTF/GLB, USD, FBX, OBJ.
- **Grafik** — SVG, PNG, EXR, vollständige PBR-Textur-Sets.
- **Fertigung** — STL und 3MF für den 3D-Druck, optional STEP für CAD und
  G-Code für CNC; Kollisionsmesh und bewegliche Gelenke bleiben erhalten.
- **Analyse** — Massen-, Schwerpunkts- und Trägheitsbericht, technisches
  Datenblatt, Schnittzeichnung als PDF.
- **Physik** — die exportierte OrniConfig enthält Konfigurationshash und
  Physikversion, damit das Verhalten überall reproduzierbar bleibt.

Damit wird der Studio-Entwurf zugleich ein flugfähiges Spielmodell, ein
Renderobjekt und eine Fertigungsvorlage für einen realen Ornithopter. Der
physische und der virtuelle Vogel sind zwei Ansichten desselben Entwurfs.

## Säule II — BORN2FLAP MARKETPLACE

### Prinzip

Der Marketplace ist der Verteilungsweg für das, was im Studio entsteht. Der
kostenlose Simulator kann **alle** Marketplace-Designs fliegen — der Umsatz
kommt nicht aus dem Fliegen, sondern aus dem Konstruieren (Studio) und dem
Handel (Marketplace-Cut). Fliegen bleibt frei, Konstruieren ist das Handwerk.

### Angebotsarten

- **Kostenlose Designs** — Community-Werke unter offener Lizenz (CC).
- **Premium-Designs** — Creators setzen einen Preis oder „pay-what-you-want".
- **Auftragsarbeiten** — später: Kommissionen und Lizenzierungen direkt zwischen
  Creator und Kunde.

### Qualität, Fairness und Reproduzierbarkeit

- Jedes Design trägt **Konfigurationshash und Physikversion**; Leaderboards und
  Rennen vergleichen gegen dieselben Werte. Manipulation am Kern ist sichtbar.
- **Klassenkennzeichnung** — Masse, Energie, Flügelfläche, Assist-Level,
  erlaubte Komponenten — macht Designs fair vergleichbar.
- **Review und Moderation** — Plausibilitätsprüfung, Meldesystem, klare
  Lizenzangaben.

### Vergütung

- Einmal-Kauf für Käufer, kein Abo, keine Laufzeitgebühr.
- Creator-freundlicher Split (Beispiel 70/30), transparente Auszahlung.
- Kein Pay-to-win: Premium-Designs unterliegen denselben Klassenregeln wie
  kostenlose; der Preis kauft Kunst und Aufwand, keinen physikalischen Vorteil.

## Wirtschaftsmodell

- **Simulator** — kostenlos, quelloffen (FOSS), bleibt Einstieg und Referenz.
- **Studio** — einmalig ~100 EUR, keine Subscription, keine Cloud-Pflicht;
  Dateien gehören dem Nutzer.
- **Marketplace** — Plattform-Cut auf Premium-Verkäufe; kostenlose Designs
  kosten nichts.
- **Kein Pay-to-win** — alle Designs unterliegen denselben Klassenregeln.

## Roadmap

1. **Hangar** — Ornithopter-Editor im Spiel (kostenlos), Grundlage der
   Konstruktion.
2. **Studio** — eigenständiges Werkzeug auf demselben Kern; Vektor-Editor,
   Texturen, Export-Matrix.
3. **Marketplace Beta** — Teilen und Einmal-Käufe, Klassenkennzeichnung,
   Konfigurationshash.
4. **Plattform** — Import/Export-Ökosystem, Fertigungsdaten, Community- und
   Auftragsmodell.

## Offene Fragen

- Lizenz der Exporte: Welche Rechte erhält der Nutzer an exportierten Modellen
  (3D-Druck, kommerzielle Nutzung)?
- Exakter Creator-Split und Auszahlungsmodell.
- Moderationsmodell in der Größenordnung eines offenen Marketplace.
- Umgang mit Physikversion-Sprüngen: Wie bleiben alte Designs über
  Physikänderungen hinweg fair und reproduzierbar?
