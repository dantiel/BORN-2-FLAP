# Package BORN2FLAP for Windows into a distributable zip.
# Builds the Haskell math DLL (GHC on Windows), cooks a Shipping Win64 build via
# RunUAT, stages the DLL beside the binary, and zips the staged package.
#
# Usage:
#   Tools/package-win.ps1 -Tag v0.1.0
#   Tools/package-win.ps1 -Tag v0.1.0 -EngineRoot V:\UE_5.8 -OutDir D:\out
param(
    [string]$Tag = "",
    [string]$EngineRoot = "V:\UE_5.8",
    [string]$OutDir = ""
)

$ErrorActionPreference = "Stop"
$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path

if (-not $Tag) {
    $Tag = (git -C $RepoRoot describe --tags --always --dirty 2>$null)
    if (-not $Tag) { $Tag = "dev" }
}
if (-not $OutDir) { $OutDir = Join-Path $RepoRoot "build\release\$Tag" }
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

$Project = Join-Path $RepoRoot "Unreal\Born2Flap\Born2Flap.uproject"
$UAT = Join-Path $EngineRoot "Engine\Build\BatchFiles\RunUAT.bat"
if (-not (Test-Path $UAT)) { throw "Unreal not found at $EngineRoot" }
Write-Host "UE_ROOT=$EngineRoot"

# --- Haskell math DLL ---------------------------------------------------------
Write-Host "== Building Haskell math DLL =="
$env:Path = "V:\Born2FlapTools\ghcup\bin;$env:Path"
$env:CABAL_DIR = "V:\Born2FlapTools\cabal"
Push-Location (Join-Path $RepoRoot "MathCore")
try {
    cabal build flib:born2flap_math
    if ($LASTEXITCODE -ne 0) { throw "cabal build failed" }
} finally {
    Pop-Location
}

$Dll = Get-ChildItem -Path (Join-Path $RepoRoot "MathCore\dist-newstyle") `
    -Filter "born2flap_math.dll" -Recurse -File | Select-Object -First 1
if (-not $Dll) { throw "built born2flap_math.dll not found" }
$ThirdParty = Join-Path $RepoRoot "Unreal\Born2Flap\Binaries\ThirdParty"
New-Item -ItemType Directory -Force -Path $ThirdParty | Out-Null
Copy-Item $Dll.FullName (Join-Path $ThirdParty "born2flap_math.dll") -Force
Write-Host "staged $($Dll.FullName)"

# --- Cook + package (Shipping) -------------------------------------------------
Write-Host "== Packaging (Shipping) =="
& $UAT BuildCookRun `
    "-project=$Project" `
    -noP4 -utf8output -unattended -nocompileeditor `
    -platform=Win64 `
    -clientconfig=Shipping -serverconfig=Shipping `
    -build -cook -stage -pak -archive `
    "-archivedirectory=$OutDir"
if ($LASTEXITCODE -ne 0) { throw "RunUAT BuildCookRun failed (exit $LASTEXITCODE)" }

# --- Stage the Haskell DLL into the package -----------------------------------
# The bridge resolves ProjectDir()/Binaries/ThirdParty/<dll> at runtime.
$PkgProject = Get-ChildItem -Path $OutDir -Filter "Born2Flap.uproject" -Recurse -File |
    Select-Object -First 1
if ($PkgProject) {
    $Dest = Join-Path $PkgProject.DirectoryName "Binaries\ThirdParty"
    New-Item -ItemType Directory -Force -Path $Dest | Out-Null
    Copy-Item (Join-Path $ThirdParty "born2flap_math.dll") $Dest -Force
    Write-Host "staged DLL into $Dest"
} else {
    Write-Warning "could not locate packaged project dir to stage the DLL"
}

# --- Zip the staged package ----------------------------------------------------
$PkgRoot = if ($PkgProject) { $PkgProject.DirectoryName } else { $null }
if (-not $PkgRoot) {
    $Exe = Get-ChildItem -Path $OutDir -Filter "Born2Flap.exe" -Recurse -File | Select-Object -First 1
    if (-not $Exe) { throw "Born2Flap.exe not found after packaging" }
    $PkgRoot = $Exe.Directory.Parent.Parent.Parent   # .../Win64/Binaries/Born2Flap -> Born2Flap
}
$Zip = Join-Path $OutDir "BORN2FLAP-$Tag-win64.zip"
Compress-Archive -Path (Join-Path $PkgRoot "*") -DestinationPath $Zip -Force
Write-Host "== Done: $Zip =="
