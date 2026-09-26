param([string]$EngineRoot = 'V:\UE_5.8', [switch]$Capture)
$ErrorActionPreference = 'Stop'
$project = (Resolve-Path (Join-Path $PSScriptRoot '../Born2Flap.uproject')).Path
$log = Join-Path (Split-Path $project) 'Saved/Logs/raven-world-test.log'
$flags = @(('"'+$project+'"'),'/Game/Ravenstonefield/Maps/RAVENSTONEFIELD','-game','-unattended','-nosplash',('-abslog="'+$log+'"'))
if ($Capture) {
    $flags += @('-B2FRavenCapture','-RenderOffscreen','-ResX=1600','-ResY=900','-NoVSync')
} else {
    $flags += @('-B2FRavenTest','-nullrhi','-nosound')
}
$process = Start-Process -FilePath (Join-Path $EngineRoot 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe') -ArgumentList $flags -WindowStyle Hidden -PassThru
if (!$process.WaitForExit(600000)) {
    Stop-Process -Id $process.Id
    throw "Ravenstonefield timed out. See $log"
}
if ($process.ExitCode -ne 0) { throw "Ravenstonefield exited with $($process.ExitCode). See $log" }
if (!$Capture -and !(Select-String -LiteralPath $log -Pattern 'RavenWorldTest PASS' -SimpleMatch)) {
    throw "Ravenstonefield world validation failed. See $log"
}
if (Select-String -LiteralPath $log -Pattern 'Failed to compile Material|Fatal error') {
    throw "Ravenstonefield rendering errors. See $log"
}
Write-Host "Ravenstonefield check passed. Log: $log"
