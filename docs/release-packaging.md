# Release-Packaging (.dmg + .exe)

Ziel: bei jedem Release automatisch eine **vorkompilierte macOS `.dmg`** und
eine **Windows `.zip`** (mit `Born2Flap.exe`) erzeugen und an das GitHub-Release
anhängen.

## Warum Self-Hosted-Runner

Zwei technische Gründe machen die gehosteten GitHub-Runner ungeeignet:

1. **Unreal Engine passt nicht auf die 14 GB** Disk der gehosteten Runner
   (UE 5.8 ist ~40–60 GB). Die Engine läuft lokal auf beiden Maschinen.
2. **Die Haskell-Native-Lib muss nativ pro Plattform gebaut werden.** GHC kann
   praktisch nicht Linux→Windows cross-kompilieren; `born2flap_math.dll`
   (Windows) und `libborn2flap_math.dylib` (macOS) entstehen auf der jeweiligen
   Maschine.

Beide Maschinen existieren bereits:

| Plattform | Maschine | Engine | Haskell |
|---|---|---|---|
| macOS | M1 Mac (arm64) | `/Volumes/Ouranos/Games/UE_5.8` | GHC 9.6.7 via ghcup (`~/.ghcup/bin`) |
| Windows | `gamayun-win` | `V:\UE_5.8` | GHC 9.10.3 via ghcup (`V:\Born2FlapTools\ghcup\bin`, `CABAL_DIR=V:\Born2FlapTools\cabal`) |

## Einmalige Einrichtung: Runner registrieren

Auf jeder Maschine einen Self-Hosted-Runner mit den passenden Labels anlegen
(die Labels müssen zu `runs-on` in `.github/workflows/release.yml` passen):

**macOS (M1):**

```bash
mkdir -p ~/actions-runner && cd ~/actions-runner
curl -o actions-runner-osx-arm64.tar.gz -L \
  https://github.com/actions/runner/releases/latest/download/actions-runner-osx-arm64-2.325.0.tar.gz
tar xzf actions-runner-osx-arm64.tar.gz
./config.sh --url https://github.com/dantiel/BORN-2-FLAP \
  --token <RUNNER_TOKEN> --name mac-m1 --labels macOS,ARM64
./run.sh            # oder: ./svc.sh install && ./svc.sh start (Dienst)
```

**Windows (`gamayun-win`):** analog mit dem `actions-runner-win-x64`-Paket und
`--labels Windows,X64`.

> Runner-Token: Repository → *Settings → Actions → Runners → New self-hosted
> runner*. Läuft der Runner als Dienst, wird der Build auch ohne offenes
> Terminal ausgeführt.

## Ablauf

1. Tag pushen: `git tag v0.2.0 && git push origin v0.2.0` (oder den Workflow
   manuell über *Actions → Release packaging → Run workflow* starten).
2. `macos-dmg` baut auf dem Mac, `windows-exe` auf `gamayun-win` — parallel.
3. Beide Jobs hängen ihr Artefakt (`*.dmg` / `*.zip`) per `gh release` an.

## Was die Skripte tun

- `Tools/package-mac.sh [tag]` — baut die arm64-Haskell-Lib, `RunUAT
  BuildCookRun -platform=Mac -configuration=Shipping`, **kopiert die dylib in
  das Paket** (`ProjectDir()/Binaries/ThirdParty/`), erzeugt die `.dmg`.
- `Tools/package-win.ps1 -Tag <tag>` — baut `born2flap_math.dll` (GHC),
  `RunUAT BuildCookRun -platform=Win64`, **kopiert die DLL in das Paket**,
  zippt den gestagten Ordner.

### Warum die Lib explizit kopiert werden muss

`Born2FlapMathBridge.cpp` lädt die Lib über den absoluten Pfad
`FPaths::ProjectDir()/Binaries/ThirdParty/<lib>`. `RunUAT` kopiert Content und
Code, aber **nicht** diese ThirdParty-Binaries — ohne den Kopier-Schritt startet
das Spiel mit „Haskell math backend missing".

## Manueller Fallback (ohne Runner)

```bash
# macOS
Tools/package-mac.sh v0.2.0
gh release upload v0.2.0 build/release/v0.2.0/*.dmg --clobber

# Windows (auf gamayun-win)
powershell -File Tools/package-win.ps1 -Tag v0.2.0
gh release upload v0.2.0 build/release/v0.2.0/*.zip --clobber
```

## Offene Punkte (später)

- **Code-Signierung/Notarisierung** (macOS Gatekeeper, Windows SmartScreen):
  Unsigned Builds zeigen Warnungen (Rechtsklick → Öffnen bzw. „Weitere
  Infos → Trotzdem ausführen"). Für ein Alpha in Ordnung.
- **`EngineAssociation`** in `Born2Flap.uproject` steht noch auf `"5.7"`
  (kosmetisch; `RunUAT`/`Build.bat` ignorieren das Feld, relevant nur für
  Doppelklick-Öffnen im Launcher).
- **Steam/Epic-Store-URLs** auf der Website sind noch Platzhalter.
