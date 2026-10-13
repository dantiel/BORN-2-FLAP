# Statue selbst anpassen

## Nur den Vogel auf die Hand setzen: direkt in Unreal

1. Projekt `Unreal/Born2Flap/Born2Flap.uproject` öffnen. Im Content Browser
   die Map `/Game/Shiomori/Maps/SHIOMORI` öffnen. Nicht im Play-Modus arbeiten.
2. Rechts im **Outliner** nach `Campus founder kestrel` suchen und diesen
   Actor auswählen. **F** im 3D-Fenster fokussiert die Auswahl.
3. **W** aktiviert Verschieben, **E** Drehen. Mit den Achsen des Gizmos arbeiten.
   Z ist die Höhe. Für die Handauflage zunächst nur wenige Zentimeter ändern.
4. Präziser geht es rechts unter **Details → Transform → Location**:
   Z in kleinen Schritten verändern. Eine Unreal-Einheit entspricht hier einem
   Zentimeter. Das Positionsraster bei Bedarf ausschalten oder auf 1 stellen.
5. Aus mindestens zwei Blickrichtungen prüfen: Von vorn kann ein Vogel passend
   aussehen, während er in der Seitenansicht neben oder über der Hand liegt.
6. Die Map mit **Strg+S** speichern. Erst anschließend Play starten.

Das verändert nur den eigenständigen Vogel-Actor. Arme, Haare und Controller
werden dadurch nicht mitbewegt. Der Ursprungs-/Drehpunkt des Vogels liegt an
der Nase, daher kann Drehen eine anschließende Positionskorrektur erfordern.

**Wichtig:** Unsere Campus-Generatoren erzeugen diese Actors neu. Eine später
ausgeführte Campus-Regeneration kann eine rein im Editor gespeicherte Änderung
überschreiben. Vorher die drei Location-Werte und die Rotation notieren oder
sie mir zum Übernehmen in die Erzeugung geben. Normales Speichern und Spielen
überschreibt sie nicht.

Offizielle Dokumentation:
- https://dev.epicgames.com/documentation/en-us/unreal-engine/outliner-in-unreal-engine
- https://dev.epicgames.com/documentation/unreal-engine/level-editor-in-unreal-engine

## Arm und Hand verändern: anderer Arbeitsschritt

Die sichtbare Figur ist eine **statische, bereits gebackene Pose**. Im Level
Editor lässt sich deshalb nicht einfach der einzelne Arm mit E drehen.

Die ursprüngliche Figur besitzt ein Skelett und liegt unverändert unter
`Unreal/Born2Flap/SourceAssets/Founder/Founder_rigged.glb`. Ich habe ihre Pose
mit `Unreal/Born2Flap/Tools/bake_founder.py` verändert. Die `aim(...)`-Aufrufe
bestimmen dort Oberarm-, Unterarm- und Handrichtung; die rechte Hand wird
zusätzlich um ihre Längsachse gedreht. Das Skript erzeugt eine neue statische
GLB und die Hand-/Halspositionen in `founder_pose.json`.

Danach muss die GLB in Unreal neu importiert und müssen Vogel, Sender und Gurt
an die neuen Handpositionen gesetzt werden. Nur das Python-Skript auszuführen
ändert die gespeicherte Map noch nicht. Für deinen ersten Versuch, lediglich
den Vogel richtig auf die Hand zu legen, ist die direkte Actor-Verschiebung
oben wesentlich einfacher.
