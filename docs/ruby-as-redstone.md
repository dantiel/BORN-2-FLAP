# Ruby — das Redstone der Software

Status: Leitprinzip · verweist auf `docs/decisions/0001-language-and-runtime-architecture.md`

## Der Satz

**Ruby ist das Redstone der Software.**

Minecraft-Redstone ist nicht die Maschine selbst — es ist der Draht, das
Signal, der Schalter. Aus wenigen, immer gleichen Bausteinen (Leitung,
Repeater, Komparator) entstehen durch Verdrahtung beliebig komplexe
Maschinen: Türen, Uhren, Rechner, Speicher. Genau diese Rolle übernimmt Ruby
im BORN-2-FLAP-Brain.

## Die drei Ebenen — wer was tut

```text
Ruby Brain      das Redstone: Regeln, Modi, Progression, Ereignisse, Plugins
Haskell Core    die Physik: Aerodynamik, Kinematik, Stabilisierung
C / Unreal C++  die Welt: Rendering, Chaos, Eingabe, Plattform, Transport
```

Ruby berechnet keinen zeitkritischen Flügelschritt. Ruby rendert kein Pixel.
Ruby **verdrahtet**: es entscheidet, welcher Modus läuft, welche Regel greift,
welches Ereignis welches Kommando auslöst.

## Warum das Redstone-Prinzip

1. **Kleine, gleiche Bausteine.** Eine Spielregel ist ein Ruby-Objekt mit
   einem schmalen Vertrag (`tick`, `on_event`, `score`, `valid?`). Wie ein
   Redstone-Komparator.
2. **Komplexität durch Verdrahtung.** Rennregeln, Punktwertung, Freischaltung,
   Anti-Cheat — alles entsteht durch Zusammenschalten einfacher Rulesets,
   nicht durch neue C++-Klassen.
3. **Reparierbarkeit.** Redstone kann man umstecken, ohne den Motor zu
   tauschen. Ruby-Regeln ändern sich im laufenden Zyklus, ohne Physikkern
   oder Engine anzufassen.
4. **Sicherheit.** Ruby-Plugins erhalten Capabilities statt Direktzugriff —
   der Sandkasten ist das Fundament des Redstones (siehe ADR 0001).
5. **Testbarkeit.** Rulesets laufen headless unter System-Ruby mit `minitest`,
   bevor sie je eine Engine sehen. Einbettung später über mruby.

## Was Ruby NIEMALS tut

- Keinen Physikschritt berechnen (Haskell).
- Kein Chaos-Rigid-Body, kein Mesh, kein Sound (Unreal/C++).
- Kein Transport pro Element pro Tick (nur aggregierte Telemetrie).

## Konsequenz für neue Features

Jede neue Spielregel, jeder Spielmodus, jede Multiplayer-Regel **beginnt als
Ruby-Ruleset**. Eine Regel in C++ benötigt einen Architecture Decision Record
(siehe ADR 0001).
