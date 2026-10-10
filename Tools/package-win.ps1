# Build a portable Windows package; no engine or compiler is needed by players.
param(
    [string]$Tag = "dev",
    [string]$EngineRoot = "V:\UE_5.8",
    [string]$OutDir = "",
    [string]$GhcBin = "V:\Born2FlapTools\ghcup\bin",
    [string]$CabalDir = "V:\Born2FlapTools\cabal",
    [string]$SevenZip = 'C:\Program Files\7-Zip\7z.exe'
)
$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest
if ($Tag -notmatch '^[A-Za-z0-9][A-Za-z0-9._-]*$') { throw 'Tag must be a filename-safe version.' }
$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
if (!$OutDir) { $OutDir = Join-Path $RepoRoot "build\release\$Tag" }
$OutDir = [IO.Path]::GetFullPath($OutDir)
if (!(Test-Path -LiteralPath $SevenZip)) { throw "7-Zip is required to build the self-extracting EXE: $SevenZip" }
foreach ($Name in @("BORN2FLAP-$Tag-win64.zip", "BORN2FLAP-$Tag-win64-extract.exe")) {
    if (Test-Path -LiteralPath (Join-Path $OutDir $Name)) { throw "Output already exists: $Name. Choose a new version or output directory." }
}
# Each run gets a new archive directory, so stale binaries cannot enter a release.
$Archive = Join-Path $OutDir ('stage-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force $Archive | Out-Null
$Project = Join-Path $RepoRoot 'Unreal\Born2Flap\Born2Flap.uproject'
$UAT = Join-Path $EngineRoot 'Engine\Build\BatchFiles\RunUAT.bat'
$AppLocal = Join-Path $EngineRoot 'Engine\Binaries\ThirdParty\AppLocalDependencies'
if (!(Test-Path $UAT) -or !(Test-Path $AppLocal)) { throw 'Engine or app-local runtime dependencies missing.' }
$env:Path = "$GhcBin;$env:Path"
$env:CABAL_DIR = $CabalDir
Push-Location (Join-Path $RepoRoot 'MathCore')
try {
    cabal build flib:born2flap_math
    if ($LASTEXITCODE) { throw 'Haskell build failed' }
    $DllPath = cabal list-bin flib:born2flap_math
    if ($LASTEXITCODE) { throw 'Cannot locate Haskell build output' }
    $DllPath = Join-Path (Split-Path $DllPath.Trim()) 'born2flap_math.dll'
    if (!(Test-Path -LiteralPath $DllPath)) { throw "Haskell DLL missing: $DllPath" }
    cabal test all
    if ($LASTEXITCODE) { throw 'Haskell tests failed' }
} finally { Pop-Location }
$ThirdParty = Join-Path $RepoRoot 'Unreal\Born2Flap\Binaries\ThirdParty'
New-Item -ItemType Directory -Force $ThirdParty | Out-Null
Copy-Item $DllPath.Trim() (Join-Path $ThirdParty 'born2flap_math.dll') -Force
& $UAT BuildCookRun "-project=$Project" -noP4 -utf8output -unattended -platform=Win64 -clientconfig=Shipping -serverconfig=Shipping -build -cook -allmaps -stage -pak -nodebuginfo -archive "-archivedirectory=$Archive" "-applocaldirectory=$AppLocal"
if ($LASTEXITCODE) { throw "Unreal packaging failed: $LASTEXITCODE" }
$Package = Join-Path $Archive 'Windows'
foreach ($Required in @('Born2Flap.exe', 'Born2Flap\Binaries\Win64\Born2Flap-Win64-Shipping.exe', 'Born2Flap\Binaries\ThirdParty\born2flap_math.dll', 'Born2Flap\Binaries\Win64\vcruntime140.dll', 'Born2Flap\Content\Paks')) {
    if (!(Test-Path (Join-Path $Package $Required))) { throw "Incomplete package: $Required" }
}
Copy-Item (Join-Path $RepoRoot 'LICENSE') $Package
Copy-Item (Join-Path $RepoRoot 'THIRD-PARTY-NOTICES.md') $Package
Copy-Item (Join-Path $RepoRoot 'Unreal\Born2Flap\Tools\fonts\OFL.txt') (Join-Path $Package 'ChakraPetch-OFL.txt')
& (Join-Path $PSScriptRoot 'test-package-win.ps1') -Package $Package
@"
BORN 2 FLAP - $Tag
Extract the entire ZIP to a folder, then double-click Born2Flap.exe.
Windows x64 and a DirectX-compatible graphics driver are required.
Space: launch. W: throttle. Shift+W: full throttle. Arrows: pitch/roll.
A/D: yaw. R: reset. F3: RC controller setup. F4: switch environment.
F8: bird and input settings. V: chase/FPV. F6: ground camera.
Experimental alpha: bank recovery and sustained flight have known failures.
"@ | Set-Content (Join-Path $Package 'README.txt')
$Commit = git -c "safe.directory=$RepoRoot" -C $RepoRoot rev-parse HEAD
if ($LASTEXITCODE) { throw 'Cannot read source commit.' }
$Dirty = git -c "safe.directory=$RepoRoot" -C $RepoRoot status --porcelain
if ($LASTEXITCODE) { throw 'Cannot read source status.' }
@{version=$Tag;commit="$Commit";dirty=[bool]$Dirty;builtUtc=[DateTime]::UtcNow.ToString('o');configuration='Shipping';packagedSmokeTests='passed'} | ConvertTo-Json | Set-Content (Join-Path $Package 'build-info.json')
& (Join-Path $PSScriptRoot 'bundle-win.ps1') -Package $Package -OutDir $OutDir -Tag $Tag -SevenZip $SevenZip
$Zip = Join-Path $OutDir "BORN2FLAP-$Tag-win64.zip"
Add-Type -AssemblyName System.IO.Compression.FileSystem
if (Test-Path $Zip) { throw "Output already exists: $Zip. Choose a new version or output directory." }
[IO.Compression.ZipFile]::CreateFromDirectory($Package, $Zip, [IO.Compression.CompressionLevel]::Optimal, $false)
& $SevenZip t $Zip
if ($LASTEXITCODE) { throw 'ZIP integrity test failed.' }
$Hash = (Get-FileHash $Zip -Algorithm SHA256).Hash.ToLowerInvariant()
"$Hash  $([IO.Path]::GetFileName($Zip))" | Set-Content "$Zip.sha256" -Encoding ASCII
Write-Host "Package ready: $Zip"
