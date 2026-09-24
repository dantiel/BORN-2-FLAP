# Unreal-Prototyp

Dieses Projekt ist für Unreal Engine 5.8.2 vorbereitet.

## Aktueller Zustand

- C++-GameMode und physikalischer Test-Pawn
- Chaos-Rigid-Body mit Chase Camera
- Eingabe für Throttle, Roll, Pitch und Yaw
- fester 240-Hz-Aufruf der Mathematik-Bridge
- dynamisches Laden der Math-C-ABI über `born2flap_math.h`
- direkte Tastatursteuerung: Space-Handstart, W-Fluegelschlag, Shift+W mehr
  Leistung, Loslassen zum Gleiten, A/D Gieren, Links/Rechts Rollen, Hoch/Runter Nicken, R Reset
- virtuelle RC-Knueppel mit Anstiegszeit, Rueckstellung und Expo; reine Luftkraefte/-momente
- Vogelmodell, Himmel, Boden, Tageslicht, Torparcours und Flug-HUD
- sichtbare Kraftvektoren und klarer Fehlertext bei fehlendem Backend
- separates Waldtal mit Terrain, Fluss, Tannen, Gras und Felsen; F4 wechselt zum alten Uebungsgelaende
- Windows-USB-/vJoy-Sender mit Live-Achsenanzeige, Kanalzuordnung und Kalibrierung unter F3

Der Haskell-Build muss seine Plattformbibliothek hier ablegen:

```text
Binaries/ThirdParty/born2flap_math.dll
Binaries/ThirdParty/libborn2flap_math.dylib
Binaries/ThirdParty/libborn2flap_math.so
```

Ohne diese Bibliothek startet das Projekt, zeigt aber einen Fehlertext und wendet keine Math-Kraft an.

## Noch erforderlich

1. Unreal Engine 5.8.2 installieren.
2. Projektdateien für `Born2Flap.uproject` generieren.
3. Die Haskell-Shared-Library mit dem dokumentierten GHC/Cabal-Setup bauen und nach
   `Binaries/ThirdParty/` kopieren.
4. mruby als Unreal-Modul einbetten und das Ruby-View-Model mit UMG verbinden.

Die beiden Level werden beim Start prozedural erzeugt. `Tools/play.ps1` startet
das Waldtal; `Tools/play.ps1 -Level Training` startet den bisherigen Torparcours.
Einrichtung des RC-Senders und Asset-Quellen:
[`docs/natural-valley-and-rc.md`](../../docs/natural-valley-and-rc.md).

Der vollständige Windows-Installations- und Buildpfad ist in [`docs/windows-development.md`](../../docs/windows-development.md) dokumentiert.

Modell, native Energietests und Unreal-Flugtests:
[`docs/rc-controls.md`](../../docs/rc-controls.md).
