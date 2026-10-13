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
Mobile cross-kompiliert. Befund der Maschine (Okt 2026):

- **Xcode 26.3 + iOS SDK 26.2** — vorhanden.
- **Android NDK** — fehlt (nicht installiert).
- **iOS-Cross-GHC 9.14.1** — installiert unter `~/ghc-ios` (prebuilt, siehe unten).

### iOS — GELÖST (Oktober 2026)

Das `ghc-ios`-Projekt wurde von **Novavero-AI** wiederbelebt und als
reproduzierbares, versioniertes Toolchain veröffentlicht:

- Repo: https://github.com/Novavero-AI/ghc-ios (BSD-3-Clause)
- Release: `ghc-9.14.1-aarch64-apple-ios.tar.gz` (242 MB) — GHC 9.14.1
  Cross-Compiler, der Haskell zu nativem **ARM64-iOS-Statik-Code** kompiliert.
- Kein Homebrew-LLVM nötig (der `libffi`-Blocker wurde upstream gefixt); nur
  Xcode + iOS SDK.

**Bewiesener End-to-End-Pfad** (`Tools/build-math-ios.sh`):

```sh
~/ghc-ios/bin/aarch64-apple-ios-ghc \
  -staticlib -no-hs-main -isrc -icbits -O2 \
  src/Born2Flap/Math/FFI.hs cbits/bridge.c -o libborn2flap_math-ios
```

Ergebnis: `libborn2flap_math-ios.a` (34.8 MB, `ar`-Archiv) mit **allen 28
C-ABI-Symbolen** (`b2f_math_*` + `hs_b2f_math_*`), Objekte `Mach-O 64-bit
object arm64`, `LC_BUILD_VERSION platform=2 (iOS) minos=15.0`. Der
`Born2FlapMathBridge.cpp` ändert sich nicht — nur die Lieferform.

**Zwei Dinge für die spätere App-Integration beachten:**

1. **Triple-Maske:** der Compiler meldet `aarch64-apple-darwin`, nicht
   `aarch64-apple-ios` (GHCs `configure` faltet `darwin|ios` zu einem
   `OSDarwin`). Cabal leitet daraus `os(ios)=false` ab. Wir umgehen das, weil
   wir GHC direkt aufrufen (kein cabal). Der erzeugte Code ist trotzdem iOS:
   `LLVM target = arm64-apple-ios15.0`.
2. **RTS-Linkzeit-Overrides** (beim Linken der **App**, nicht der `.a`):
   - `mprotectForLinker` als No-op (iOS W^X),
   - `osReserveHeapMemory` memory-pressure-aware (GHC verlangt standardmäßig
     1 TiB Adressraum; iOS gewährt ~1 GB),
   - `os_log`-Routing für RTS-Diagnose.
   Diese drei liefert nova-kit als Plattform-Shell; sie gehören in den
   iOS-Unreal-Link-Schritt, sobald die `.a` eingebettet wird.

### Was bereits bewiesen ist (statischer Link-Pfad)

1. `cabal` kann auf macOS **keine** `native-static`-Foreign-Library bauen
   (`We can currently only build shared foreign libraries on OSX`).
2. GHCs `-staticlib`-Flag funktioniert direkt und erzeugt ein eigenständiges
   Archiv (RTS + base + math gebündelt) mit allen C-ABI-Symbolen
   (`b2f_math_*`, `hs_b2f_math_*`). Desktop: `Tools/build-math-static.sh`;
   iOS: `Tools/build-math-ios.sh`.
3. `cbits/bridge.c` legt bereits das `ghc-ios`-Muster an (`hs_init`, RTS einmalig,
   `hs_b2f_math_*`-Wrapper) — exakt die Struktur für statisches Linken.

### Android — möglich, aber Setup nötig

GHC trägt Android offiziell. Fehlt: Android NDK + Cross-GHC
(`aarch64-linux-android`). Cross-GHC liegt nicht im ghcup-Default-Channel;
Beschaffung über `ghc-android`/`haskell.nix` oder Community-Bindist.

## Empfehlung

- **Desktop (macOS/Win/Linux):** bleibt beim geteilten `.dylib`/`.dll`/`.so` via
  `flib:born2flap_math`.
- **iOS:** Weg B jetzt umsetzbar — `Tools/build-math-ios.sh` erzeugt die
  statische `.a` direkt aus der kanonischen Haskell-Quelle. Der nächste Schritt
  ist, diese `.a` in den iOS-Unreal-Build zu hängen (plus die drei
  RTS-Linkzeit-Overrides oben). Der C++-Fallback
  (`Native/src/born2flap_math_bridge.cpp`) bleibt als A/B-Referenz, ist aber
  nicht mehr die einzige iOS-Option.
- **Android:** Weg B möglich — NDK + Cross-GHC installieren, dann
  `Tools/build-math-static.sh --target=aarch64-linux-android` bzw. `.so`-Build.
