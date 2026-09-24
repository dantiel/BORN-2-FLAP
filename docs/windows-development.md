# Windows-Entwicklungsumgebung

Dieses Dokument beschreibt die aktuell verwendete Windows-Umgebung und den reproduzierbaren Installations- und Buildpfad für BORN-2-FLAP.

## Geprüfte Umgebung

Toolchain am 6. September eingerichtet; Windows-Builds und Flugtests am 24. September 2026 erneut geprueft.

| Komponente | Version oder Pfad |
| --- | --- |
| Windows | Windows 11 Pro 23H2, Build 22631.6199, x64 |
| Visual Studio Build Tools | 2022 Build Tools 17.14.39 |
| MSVC | 14.44.35207 / Compiler 19.44.35228 |
| Windows SDK | 10.0.22621.0 (10.0.26100.0 ist ebenfalls installiert) |
| CMake | 4.4.3 |
| Ninja | über WinGet installiert |
| Ruby | 3.3.12 x64-mingw-ucrt |
| GHC | 9.10.3 |
| Cabal | 3.16.1.0 |
| Unreal Engine | 5.8.2 unter `V:\UE_5.8` |
| C:-Freiraum | veraenderlich; vor Installationen erneut pruefen |

Die Visual-Studio-Installation wird über diese Toolchain initialisiert:

```powershell
cmd /c 'call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" && cl'
```

GHC und Cabal liegen unter `V:\Born2FlapTools\ghcup\bin`. Für Cabal wird ein getrennter Cache verwendet:

```powershell
$env:Path = 'V:\Born2FlapTools\ghcup\bin;' + $env:Path
$env:CABAL_DIR = 'V:\Born2FlapTools\cabal'
```

## Installation

1. Visual Studio 2022 Build Tools mit `Microsoft.VisualStudio.Workload.VCTools` und den empfohlenen Komponenten installieren.
2. CMake und Ninja installieren.
3. Ruby 3.3 x64 installieren.
4. GHC und Cabal über GHCup nach `V:\Born2FlapTools\ghcup` installieren.
5. Unreal Engine 5.8 installieren und unter `V:\UE_5.8` verfügbar machen.
6. Vor einer groesseren Installation mindestens 10 GB auf C: freihalten. Die Ruhezustandsdatei kann bei Bedarf mit Administratorrechten deaktiviert werden, um Platz freizugeben:

   ```powershell
   powercfg /hibernate off
   ```

   Das lässt sich mit `powercfg /hibernate on` rückgängig machen.

## Builds und Tests

### C++-First-Flap

```powershell
cmake -S . -B build-vs -G "Visual Studio 17 2022" -A x64
cmake --build build-vs --config Debug --parallel
ctest --test-dir build-vs -C Debug --output-on-failure
```

### Ruby-Brain

```powershell
ruby -I Brain/lib Brain/test/brain_test.rb
```

### Haskell Math Core

```powershell
$env:Path = 'V:\Born2FlapTools\ghcup\bin;' + $env:Path
$env:CABAL_DIR = 'V:\Born2FlapTools\cabal'
Push-Location MathCore
cabal test all
Pop-Location
```

### Unreal

```powershell
& 'V:\UE_5.8\Engine\Build\BatchFiles\Build.bat' Born2FlapEditor Win64 Development `
  -Project="$PWD\Unreal\Born2Flap\Born2Flap.uproject" -WaitMutex

& 'V:\UE_5.8\Engine\Build\BatchFiles\Build.bat' Born2Flap Win64 Development `
  -Project="$PWD\Unreal\Born2Flap\Born2Flap.uproject" -WaitMutex
```

Der Unreal-Prototyp erwartet `Unreal/Born2Flap/Binaries/ThirdParty/born2flap_math.dll`. `cabal build flib:born2flap_math` erzeugt die Haskell-DLL. `MathCore/cbits/bridge.c` exportiert die sechs Funktionen aus `Native/include/born2flap_math.h` und initialisiert die GHC-Runtime, bevor Unreal den Haskell-Fahrzeugzustand verwendet.

## Spielen und automatischer Flugtest

Aus dem Repository-Stamm, mit lokal installiertem Unreal 5.8.2:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File Unreal/Born2Flap/Tools/play.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File Unreal/Born2Flap/Tools/test-flight.ps1
```

Beide Skripte akzeptieren `-EngineRoot` fuer einen anderen Installationspfad.
`Bypass` gilt nur fuer diesen Skriptprozess; die Systemrichtlinie wird nicht veraendert.
Der Starter verwendet `UnrealEditor.exe -game`, weil der Development-Spielbuild
ungekochte Inhalte nicht direkt laden kann. Ein gepacktes Distributionspaket ist
noch nicht Bestandteil dieses Stands.

Das Testskript faehrt mit der echten Haskell-DLL und Chaos jeweils 75 Sekunden
bei 30, 60 und 144 Simulationsbildern/s: Ruhe am Boden, Handstart, normaler
Fluegelschlag, Gleiten, staerkerer Fluegelschlag, Hochziehen, Kurve, Ausgleiten
bis zur Landung, R-Reset und zweiter Start. Ein zusaetzlicher 180-Sekunden-Test
prueft Dauerflug mit Kurven. Die Skripte pruefen Exitcode **und** PASS-Zeile.
Die Tests ersetzen die Eingabequelle; Rendering und tatsaechliche Tastaturereignisse
werden zusaetzlich im sichtbaren Spiel geprueft.

Logs: `Unreal/Born2Flap/Saved/Logs/flight-test-*.log`, `flight-soak.log` und
`play-training.log`. `FlightTelemetry` enthaelt Flughoehe, Geschwindigkeit,
Steigrate, Orientierung, Leistung, Batteriezustand, Fluegelwinkel sowie
getrennte Luftkraefte, Luftmomente und Lageassistenzmomente. `FlightImpact`
nennt das getroffene Objekt bei groesseren Kollisionen oberhalb des Bodens.

Der native Energietest braucht kein Unreal und nutzt dieselbe Antriebsauswahl:

```powershell
python Native/tests/abi_smoke.py Unreal/Born2Flap/Binaries/ThirdParty/born2flap_math.dll
python Native/tests/flight_regression.py Unreal/Born2Flap/Binaries/ThirdParty/born2flap_math.dll
python Native/tests/aerodynamic_flight.py Unreal/Born2Flap/Binaries/ThirdParty/born2flap_math.dll
```

Die kleinen Materialdateien unter `Content/Training` sind versioniert. Sie lassen
sich mit `Tools/create_training_palette.py` und Unreals PythonScriptPlugin neu
erzeugen. Alle Szenenlichter und Geometrien werden beweglich erzeugt; die Szene
benoetigt keinen Lightmass-Build.

## Aktueller Spielstand und Grenzen

Der **fluegelgetriebene Prototyp** enthaelt einen prozeduralen Vogel,
bewegte Fluegel aus den echten Servowinkeln, Himmel, Boden, HUD und sechs Tore.

| Eingabe | Wirkung |
| --- | --- |
| Space am Boden | Einmaliger Handstart mit Anfangsfahrt |
| W halten | Kontinuierlicher normaler Fluegelschlag |
| Shift + W halten | Staerkerer und schnellerer Fluegelschlag |
| W loslassen | Gleitflug, dabei gehen Energie und Hoehe verloren |
| A / D | Kurvenneigung |
| Pfeil hoch / runter (auch S) | Nase heben / senken; Hochziehen kostet Fahrt |
| R | Koerper und gesamten Firmwarezustand zuruecksetzen |
| F1 | Kraftpfeile: cyan = Aerodynamik, rot = Gewicht |
| F2 | Lageassistent an / aus |

Chaos erhaelt ausschliesslich die berechneten Luftkraefte und Schwerkraft.
Der Lageassistent liefert begrenzte Drehmomente; es gibt keinen Hoehen- oder
Geschwindigkeitsregler und keine gesperrten Rotationsachsen. Am Feldrand
steuert der aktivierte Lageassistent zurueck. Ohne Fluegelschlag kann er einen
Sinkflug nicht verhindern. Unassistierter Flug, reale Servo-/Getriebedaten und
die aerodynamischen Koeffizienten brauchen weitere Kalibrierung. Modell,
Messwerte und Grenzen stehen im [aktuellen Flugbericht](wing-powered-flight.md).
