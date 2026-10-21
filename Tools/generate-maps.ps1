param(
    [string]$EngineRoot = $env:UE_ROOT,
    [string[]]$Maps,
    [switch]$Force,
    [switch]$SkipBuild,
    [switch]$Plan
)
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
if (!$EngineRoot) {
    foreach ($candidate in @('V:\UE_5.8', 'C:\Program Files\Epic Games\UE_5.8')) {
        if (Test-Path (Join-Path $candidate 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe')) { $EngineRoot=$candidate; break }
    }
}
if (!$EngineRoot) { throw 'Set UE_ROOT or pass -EngineRoot.' }
$python = Join-Path $EngineRoot 'Engine/Binaries/ThirdParty/Python3/Win64/python.exe'
if (!(Test-Path $python)) { throw "Unreal bundled Python not found: $python" }
$arguments = @((Join-Path $repo 'Tools/generate_maps.py'), '--engine-root', $EngineRoot)
if ($Maps) { $arguments += '--maps'; $arguments += $Maps }
if ($Force) { $arguments += '--force' }
if ($SkipBuild) { $arguments += '--skip-build' }
if ($Plan) { $arguments += '--plan' }
& $python @arguments
if ($LASTEXITCODE) { throw "Map generation failed ($LASTEXITCODE). See Unreal/Born2Flap/Saved/MapGeneration." }
