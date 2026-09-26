# Build both sides of the physics ABI together before launching Unreal.
param(
    [string]$EngineRoot = 'V:\UE_5.8',
    [string]$GhcBin = 'V:\Born2FlapTools\ghcup\bin',
    [string]$CabalDir = 'V:\Born2FlapTools\cabal'
)
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '../../..')).Path
$project = Join-Path $repo 'Unreal/Born2Flap/Born2Flap.uproject'
$oldPath = $env:Path
$oldCabalDir = $env:CABAL_DIR
try {
    $env:Path = "$GhcBin;$env:Path"
    $env:CABAL_DIR = $CabalDir
    Push-Location (Join-Path $repo 'MathCore')
    try {
        # Windows PowerShell wraps redirected native stderr as errors, even for warnings.
        $ErrorActionPreference = 'Continue'
        & cabal build flib:born2flap_math
        $ErrorActionPreference = 'Stop'
        if ($LASTEXITCODE) { throw 'Physics backend build failed.' }
        $dll = & cabal list-bin flib:born2flap_math
        if ($LASTEXITCODE) { throw 'Cannot locate the physics backend.' }
        # Cabal 3.16 reports libborn2flap_math.dll, but GHC emits the Windows name without lib.
        $dll = Join-Path (Split-Path $dll.Trim()) 'born2flap_math.dll'
        if (!(Test-Path -LiteralPath $dll)) { throw "Physics build output missing: $dll" }
    } finally { Pop-Location }
    $destination = Join-Path $repo 'Unreal/Born2Flap/Binaries/ThirdParty/born2flap_math.dll'
    New-Item -ItemType Directory -Force (Split-Path $destination) | Out-Null
    if (!(Test-Path $destination) -or (Get-FileHash $dll.Trim()).Hash -ne (Get-FileHash $destination).Hash) {
        try { Copy-Item -LiteralPath $dll.Trim() -Destination $destination -Force }
        catch { throw "Cannot update physics DLL. Close the game/editor, then retry. $($_.Exception.Message)" }
    }
    $ErrorActionPreference = 'Continue'
    & (Join-Path $EngineRoot 'Engine/Build/BatchFiles/Build.bat') Born2FlapEditor Win64 Development "-Project=$project" -WaitMutex -NoHotReloadFromIDE
    $ErrorActionPreference = 'Stop'
    if ($LASTEXITCODE) { throw 'Unreal build failed.' }
} finally {
    $env:Path = $oldPath
    $env:CABAL_DIR = $oldCabalDir
}
