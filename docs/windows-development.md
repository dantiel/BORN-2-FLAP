# Windows-Entwicklungsumgebung

Dieses Dokument beschreibt die aktuell verwendete Windows-Umgebung und den reproduzierbaren Installations- und Buildpfad für BORN-2-FLAP.

## Geprüfte Umgebung

Die Angaben wurden am 6. September 2026 auf dem Entwicklungsrechner geprüft.

| Komponente | Version oder Pfad |
| --- | --- |
| Windows | Windows 10 Pro 23H2, Build 22631.6199, x64 |
| Visual Studio Build Tools | 2022 Build Tools 17.14.39 |
| MSVC | 14.44.35207 / Compiler 19.44.35228 |
| Windows SDK | 10.0.22621.0 (10.0.26100.0 ist ebenfalls installiert) |
| CMake | 4.4.3 |
| Ninja | über WinGet installiert |
| Ruby | 3.3.12 x64-mingw-ucrt |
| GHC | 9.10.3 |
| Cabal | 3.16.1.0 |
| Unreal Engine | 5.8.2 unter `V:\UE_5.8` |
| C:-Freiraum | bei der Prüfung etwa 12,8 GB |

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
6. Vor einer größeren Installation mindestens 10 GB auf C: freihalten. Falls Windows während der Installation die Ruhezustandsdatei benötigt, kann sie temporär deaktiviert werden:

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

Der Unreal-Prototyp erwartet `Unreal/Born2Flap/Binaries/ThirdParty/born2flap_math.dll`. Der aktuelle Build enthält eine native C-ABI-Bridge mit den sechs Funktionen aus `Native/include/born2flap_math.h`; der Haskell Math Core wird separat mit Cabal getestet.

## Bekannte Grenzen

Der aktuelle Unreal-Prototyp ist eine erste technische Szene: ein physikalischer Quader, Kamera, Boden, Licht und Tastatursteuerung. Die Lift- und Momentenwerte sind bewusst einfache Prototypwerte. Flügelgeometrie, sichtbare Schlagbewegung, aerodynamische Kräfte und eine vollständige Haskell-Laufzeitkopplung sind noch nicht als fertige Spielmechanik umgesetzt.

Die Meldung `DumpUnbuiltLightInteractions` stammt von dynamisch erzeugtem Licht beziehungsweise Geometrie und ist für diese Testszene eine Beleuchtungswarnung, kein Flugphysikfehler.
