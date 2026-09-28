# Mobile Mathe-Backend — Realität & Rezept

**Frage:** Können wir `libborn2flap_math.dylib` auf Mobile (iOS/Android) einsetzen?
**Antwort:** Nein — das `.dylib` ist ein Mach-O-Dynamic-Library (macOS-spezifisch).
Mobile verlangt fundamental andere Lieferformen:

| Plattform | Gebraucht | Haken |
|---|---|---|
| iOS | **statisch** gelinkte `.a`/`.framework` | `dlopen` von Drittanbieter-Dylibs ist verboten; alles muss in die App-Binary. |
| Android | ELF `.so` pro ABI (`arm64-v8a` …) via NDK | Mach-O lädt nicht. |

## Weg B — Haskell als Quelle (GHC cross-kompilieren)

Gewählte Richtung: die kanonische Physik bleibt in Haskell; der FFI wird für
Mobile cross-kompiliert. Befund der Maschine (Sep 2026):

- **Xcode 26.3 + iOS SDK 26.2** — vorhanden.
- **Android NDK** — fehlt (nicht installiert).
- **Cross-GHC** (iOS oder Android) — fehlt; nur Host-GHC 9.6.7.

### iOS — HART BLOCKIERT

GHCs iOS-Backend ist seit **GHC 8.x ungepflegt** (das `ghc-ios`-Projekt ist
eingeschlafen). Es existiert **kein** GHC-9.6-Cross-Compiler für
`aarch64-apple-ios`. Damit ist Weg B auf iOS mit dem aktuellen Toolchain
**nicht umsetzbar**, ohne das `ghc-ios`-Projekt selbst wiederzubeleben
(Forschungsaufwand, upstream broken).

### Was bereits bewiesen ist (statischer Link-Pfad)

1. `cabal` kann auf macOS **keine** `native-static`-Foreign-Library bauen
   (`We can currently only build shared foreign libraries on OSX`).
2. GHCs `-staticlib`-Flag funktioniert direkt und erzeugt ein eigenständiges
   Archiv (RTS + base + math gebündelt), 27.8 MB, mit allen C-ABI-Symbolen
   (`b2f_math_*`, `hs_b2f_math_*`). Rezept: `Tools/build-math-static.sh`.
3. `cbits/bridge.c` legt bereits das `ghc-ios`-Muster an (`hs_init`, RTS einmalig,
   `hs_b2f_math_*`-Wrapper) — exakt die Struktur für statisches Linken.

### Android — möglich, aber Setup nötig

GHC 9.6 trägt Android offiziell. Fehlt: Android NDK + Cross-GHC
(`aarch64-linux-android`). Cross-GHC liegt nicht im ghcup-Default-Channel;
Beschaffung über `ghc-android`/`haskell.nix` oder Community-Bindist.

## Empfehlung (hybrid, pragmatisch)

- **Desktop (macOS/Win/Linux):** bleibt beim geteilten `.dylib`/`.dll`/`.so` via
  `flib:born2flap_math`.
- **Android:** Weg B möglich — NDK + Cross-GHC installieren, dann
  `Tools/build-math-static.sh --target=aarch64-linux-android` bzw. `.so`-Build.
- **iOS:** Weg B derzeit blockiert → interimistisch den **C++-Fallback**
  (`Native/src/born2flap_math_bridge.cpp`) auf kanonische Parität heben, oder
  `ghc-ios` wiederbeleben. Der Unreal-Bridge (`Born2FlapMathBridge.cpp`) ist
  ABI-abstrakt — der aufrufende Code ändert sich nicht, nur die Lieferform.
