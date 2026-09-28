# Release packaging

Windows releases contain a cooked Unreal Shipping build, the Haskell physics
DLL, game assets and app-local Windows runtime libraries. Players do not need
Unreal Engine, Haskell, Visual Studio or 7-Zip installed.

## Windows downloads

- `BORN2FLAP-<tag>-win64-extract.exe`: one self-extracting download. Run it,
  choose an empty writable folder, extract, then run `Born2Flap.exe` there.
- `BORN2FLAP-<tag>-win64.zip`: portable alternative with the same game files.
  Extract the entire archive and run `Born2Flap.exe`.
- Each download has a `.sha256` checksum alongside it.

The game itself is not a single-file executable: keep its extracted data and
DLL folders together. The EXE download is a 7-Zip self-extractor, not an
installer; it creates no shortcuts or uninstaller. Its license and source
location are included. Debug symbols are excluded from player downloads.

## Build locally on Windows

Required on the build machine:

- Unreal Engine 5.8 at `V:\UE_5.8`, Visual Studio C++ tools and Windows SDK.
- GHC/Cabal at `V:\Born2FlapTools\ghcup\bin`, with
  `CABAL_DIR=V:\Born2FlapTools\cabal`.
- 7-Zip including `7z.exe`, `7z.sfx` and `License.txt` at
  `C:\Program Files\7-Zip` (override with `-SevenZip`).
- Git and Git LFS, with the real LFS asset contents checked out.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File Tools/package-win.ps1 -Tag alpha-20260927.1
```

Output: `build/release/<tag>/`. Existing download filenames are never
overwritten; use a new tag or `-OutDir` for another build.

The pipeline:

1. Builds the Haskell DLL and runs its tests.
2. Builds and cooks the Unreal Shipping target, staging into a fresh directory.
3. Checks the launcher, Shipping executable, native DLL, runtime and cooked data.
   `Born2Flap.Build.cs` stages the physics DLL as a NonUFS runtime dependency.
4. Runs the packaged Ravenstonefield flight and desktop-input smoke checks.
   These launch the Shipping executable, not the editor. Shipping logging is
   disabled; the in-game checks signal success/failure through their exit code.
   The test script enforces a timeout and writes a result JSON outside the package.
   It removes developer-tool directories from the child process PATH.
5. Adds controls, known alpha limitations, license and source build metadata
   (including whether the working tree has uncommitted changes).
6. Creates and integrity-tests the self-extracting EXE and ZIP, then hashes both.

Additional rendered validation on a machine with a compatible GPU:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File Tools/test-package-win.ps1 -Package '<stage-directory>\Windows' -Render
```

`Tools/bundle-win.ps1` can also wrap an existing cooked Windows directory as
an EXE without rebuilding; it does not replace the packaged-game smoke tests.

## GitHub workflow

`.github/workflows/release.yml` uses self-hosted runners with the engine and
native toolchain installed:

| Platform | Runner labels | Additional tools |
| --- | --- | --- |
| Windows | `self-hosted, Windows, X64` | Windows PowerShell, 7-Zip |
| macOS | `self-hosted, macOS, ARM64` | Native GHC/Cabal, Unreal, `hdiutil` |

Register and start the runners using GitHub repository Settings > Actions >
Runners. Their registration/online status must be verified separately; a
successful local package build does not establish that hosted automation works.

Pushing a `v*` tag selects both platforms. A manual workflow run accepts an
existing tag and a platform choice (`windows`, `macos`, or `both`), defaulting
to Windows. Checkout downloads LFS assets. The workflow has `contents: write`
permission and attaches the downloads to a **draft** GitHub release for review.
No workflow or release is triggered by running the local packaging script.

The macOS job continues to use `Tools/package-mac.sh <tag>` to build and wrap
its app as a DMG. Windows validation does not validate the macOS pipeline.

## Release status and limits

The 27 September 2026 local Windows release `alpha-20260927.1` completed the
pipeline: Haskell tests, Shipping build/cook, packaged smoke checks, EXE/ZIP
creation and archive integrity checks. The EXE is 473,339,084 bytes and the ZIP
519,462,617 bytes, with verified SHA-256 sidecars. The actual self-extractor
was run into a fresh folder; both game checks then passed again with offscreen
rendering and only Windows system directories on PATH. This is still a test
on the development machine, not a clean Windows installation.

Artifacts are in `build/release/alpha-20260927.1/`; the build log is
`.setup/release-alpha-20260927.1.log`. The package's `build-info.json` records
commit `823ff14b4fd3ebad807b292543f33f2a4bfc8af1` and a dirty working tree
(the packaging changes were not committed). Nothing was uploaded to GitHub.

This is an experimental alpha. Full flight regressions currently include
bank-recovery, endurance and scripted-flight failures; see
[RC controls and flight validation](rc-controls.md). Packaging smoke checks do
not certify those mechanics or replace testing on a clean player machine.

Downloads are unsigned. Code signing and a conventional installer remain
future work. The source-code MIT license does not replace the separate
Unreal or asset licenses; asset provenance is documented in
[Ravenstonefield](ravenstonefield.md) and [the natural valley](natural-valley-and-rc.md).
