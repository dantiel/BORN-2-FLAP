# Waldtal und direkter RC-Senderanschluss

Das Spiel startet im Waldtal. **F4** wechselt zwischen Waldtal und dem erhaltenen
Uebungsgelaende mit Toren. Beim Wechsel beginnt ein neuer Flug an der Startstelle.
Mit `Tools/play.ps1 -Level Training` laesst sich das alte Level direkt starten.

Das Waldtal hat modelliertes Relief, eine ebene Startwiese, einen gewundenen Fluss,
Tannen, Gras und moosige Felsen. Terrain und nahe Baumstaemme haben Kollision.
Kleine Felsen und Laub sind Dekoration. Die Hoehenanzeige misst den Abstand zum
Terrain unter dem Vogel; eine Wasserlandung bringt ihn zur Startwiese zurueck.
Wasser ist eine animierte Oberflaeche, keine Stroemungs- oder Schwimmsimulation.
Die Flugphysik und das Tragflaechenmodell bleiben unveraendert.

## RC-Sender unter Windows

Unterstuetzt werden USB-Sender im **Joystick/HID-Modus**, Simulator-Dongles und
virtuelle DirectInput-Geraete wie vJoy. Es werden keine zusaetzlichen Treiber
installiert. Eine reine Ladeverbindung oder eine serielle CRSF-Verbindung meldet
sich nicht als Joystick und ist nicht durch diesen Adapter abgedeckt.

1. Sender mit einem Daten-USB-Kabel verbinden und am Sender Joystick-Modus waehlen.
2. Im Spiel **F3** oeffnen; mit **TAB** das richtige Geraet auswaehlen. Die acht
   Live-Balken zeigen, welche Achsen sich bewegen.
3. **C** startet die Kalibrierung: Gas auf Minimum und die anderen Knueppel neutral,
   dann **ENTER**. Anschliessend alle vier Kanaele bis zu beiden Anschlaegen bewegen
   und erneut **ENTER** druecken.
4. Nacheinander nur den angezeigten Kanal in seine positive Richtung bewegen:
   Vollgas, Roll rechts, Hoehenruder ziehen, Yaw rechts. Jeden Schritt mit **ENTER**
   bestaetigen. Die anderen Kanaele neutral halten, Gas jeweils wieder unten.
5. Die Zuordnung, Richtung, Anschlaege und Neutralpunkte werden gespeichert.
   Gas auf Minimum, **F3** schliessen und mit **SPACE** handstarten.

**G** wechselt in der Einrichtung zwischen Sender und Tastatur. **X** verwirft eine
laufende Kalibrierung. **L** lernt optional einen Sender-Taster fuer den Handstart,
**K** einen anderen fuer Reset: Taste auf der Tastatur, dann den gewuenschten
Sender-Taster betaetigen. Ohne Taster bleiben SPACE und R verfuegbar.

Die Profile liegen lokal in `Unreal/Born2Flap/Saved/Config/RcControllers.ini` und
werden anhand der DirectInput-Geraeteinstanz zugeordnet. Es gibt keine feste
AETR-/TAER-Annahme. Doppelte Kanalzuordnungen und unzureichender Ausschlag werden
abgelehnt. Beim Sender wird die Tastatur-Rampe nicht angewendet; die Achsen sind
linear mit 2.5% Neutralzone fuer die drei Steuerkanaele. Gas hat keine Neutralzone.

Bei Verbindungsverlust, Fokusverlust oder geoeffneter Einrichtung werden Gas und
Steuerkanaele neutral. Nach Rueckkehr muss Gas kurz auf Minimum stehen. Das Spiel
wechselt bei Signalverlust nicht unbemerkt zur Tastatur. Eine unveraendert
angeschlossene USB-Schnittstelle ohne Funkempfang kann der Simulator nicht als
Funkverlust erkennen: die Failsafe-Ausgabe des Senders/Dongles ist dafuer zustaendig.

## Assets und Reproduktion

Die Naturmodelle und Texturen stammen von Poly Haven und sind unter
[CC0](https://polyhaven.com/license) veroeffentlicht:

- [Fir Sapling Medium](https://polyhaven.com/a/fir_sapling_medium)
- [Grass Medium 01](https://polyhaven.com/a/grass_medium_01)
- [Rock Moss Set 01](https://polyhaven.com/a/rock_moss_set_01)
- [Aerial Grass Rock](https://polyhaven.com/a/aerial_grass_rock)
- [Forest Ground 04](https://polyhaven.com/a/forest_ground_04)
- [Rock 04](https://polyhaven.com/a/rock_04)

`fetch_nature_assets.py` laedt die Quelldateien in das ignorierte Verzeichnis
`Saved/NatureSource` und prueft ihre vom Anbieter angegebenen MD5-Pruefsummen.
`manifest.json` dort enthaelt Downloadadressen und Pruefsummen. Die importierten
`.uasset`-Dateien in `Content/Nature` sind im Repository und brauchen zum Spielen
keine Internetverbindung. Terrain und Pflanzenverteilung werden deterministisch
aus `Born2FlapValley.cpp` erzeugt. Es gibt vier Mesh-Detailstufen und entfernungs-
abhaengige Ausblendung fuer Vegetation.

```powershell
python Unreal/Born2Flap/Tools/fetch_nature_assets.py
& 'V:/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe' `
  'V:/BORN 2 FLAP/Unreal/Born2Flap/Born2Flap.uproject' `
  -run=pythonscript `
  '-script=V:/BORN 2 FLAP/Unreal/Born2Flap/Tools/create_nature_assets.py' `
  '-EnablePlugins=PythonScriptPlugin,EditorScriptingUtilities' -unattended -nullrhi
```

Der Import benoetigt kein Rendering. Die Shader-Parallelitaet ist begrenzt, damit
der erste Spielstart nicht gleichzeitig fast alle CPU-Kerne mit speicherintensiven
Compilern belegt. Der Windows-Adapter verwendet die dokumentierten
[DirectInput-Geraeteschnittstellen](https://learn.microsoft.com/en-us/previous-versions/windows/desktop/ee417799(v=vs.85)).

## Pruefungen

- CTest: virtuelle Tastatur sowie RC-Kalibrierung, asymmetrische Neutralpunkte,
  Kanalumkehr, doppelte Belegungen und Wiederverbindung mit hohem Gas.
- `build-vs/Tests/Release/rc_device_probe.exe`: Geraete und Achsen ueber denselben
  DirectInput-Adapter wie im Spiel lesen.
- Der bestehende automatisierte 75-Sekunden-Flug waehlt weiterhin das flache
  Uebungslevel; Terrain und Pflanzen veraendern diesen Physikvergleich nicht.

Die abschliessenden Laufzeitergebnisse werden nach dem Build hier ergaenzt.
