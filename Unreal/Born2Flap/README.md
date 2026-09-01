# Unreal-Prototyp

Dieses Projekt ist für Unreal Engine 5.7 vorbereitet.

## Aktueller Zustand

- C++-GameMode und physikalischer Test-Pawn
- Chaos-Rigid-Body mit Chase Camera
- Eingabe für Throttle, Roll, Pitch und Yaw
- fester 240-Hz-Aufruf der Mathematik-Bridge
- dynamisches Laden des Haskell-Backends über `born2flap_math.h`
- sichtbare Kraftvektoren und klarer Fehlertext bei fehlendem Backend

Der Haskell-Build muss seine Plattformbibliothek hier ablegen:

```text
Binaries/ThirdParty/born2flap_math.dll
Binaries/ThirdParty/libborn2flap_math.dylib
Binaries/ThirdParty/libborn2flap_math.so
```

Ohne diese Bibliothek startet das Projekt, wendet aber absichtlich keine erfundene Ersatzkraft an.

## Noch erforderlich

1. Unreal Engine 5.7 installieren.
2. Projektdateien für `Born2Flap.uproject` generieren.
3. Haskell-C-ABI vollständig implementieren und paketieren.
4. mruby als Unreal-Modul einbetten und das Ruby-View-Model mit UMG verbinden.
5. eine eigene Testmap mit Boden, Licht und PlayerStart als `.umap` speichern.
