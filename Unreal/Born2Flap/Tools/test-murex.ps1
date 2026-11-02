param([string]$EngineRoot = 'V:\UE_5.8', [switch]$NullRhi,
      [ValidateRange(-1,4)][int]$BirdModel = -1, [string]$Preferences = '',
      [ValidateRange(240,3840)][int]$PhysicsHz = 240)
# Murex Spire Aerodrome runtime smoke + flight validation.
# Hard checks (level-specific): the map boots as the aerodrome (READY AT MUREX),
# both flight-camera screenshots render, and no material/fatal errors occur.
# Architectural views/collision are checked separately by capture_murex_views.py.
$ErrorActionPreference = 'Stop'
$project = (Resolve-Path (Join-Path $PSScriptRoot '../Born2Flap.uproject')).Path
$log = Join-Path (Split-Path $project) 'Saved/Logs/murex-test.log'
$arguments = @(('"' + $project + '"'), '/Game/Murex/Maps/MUREX', '-game', '-unattended', '-nosplash',
    '-B2FFlightTest', '-B2FLevel=Murex', '-nosound',
    ('-abslog="' + $log + '"'))
$arguments += @('-benchmark', '-fps=60', ('-B2FPhysicsHz='+$PhysicsHz))
if ($BirdModel -ge 0) { $arguments += '-B2FTestBirdModel='+$BirdModel }
if ($Preferences) {
    $preferencesPath = (Resolve-Path -LiteralPath $Preferences).Path
    $arguments += @('-B2FUseSavedFlightSettings', ('-B2FFlightPreferences="'+$preferencesPath+'"'))
}
if ($NullRhi) { $arguments += '-nullrhi' }
else { $arguments += @('-B2FCapture', '-RenderOffscreen', '-ResX=1600', '-ResY=900', '-NoVSync') }
$stderr = Join-Path (Split-Path $project) 'Saved/Logs/murex-test.stderr.log'
$process = Start-Process -FilePath (Join-Path $EngineRoot 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe') -ArgumentList $arguments -WindowStyle Hidden -PassThru -RedirectStandardError $stderr
while (!$process.HasExited -and (Get-Date) -lt $process.StartTime.AddMinutes(10)) {
    $null = $process.WaitForExit(1000)
}
if (!$process.HasExited) {
    Stop-Process -Id $process.Id
    throw "Murex test timed out. See $log"
}
# NOTE: $process.ExitCode is unreliable in PowerShell 5.1 when Start-Process
# redirects stderr (returns empty). The engine's own "Log file closed" line is
# the orderly-shutdown marker instead.
if (!(Select-String -LiteralPath $log -Pattern 'READY AT MUREX' -SimpleMatch)) {
    throw "Murex level did not boot as the aerodrome. See $log"
}
if (!$NullRhi -and !(Select-String -LiteralPath $log -Pattern 'Tracing Screenshot "AerodynamicFlight" taken' -SimpleMatch)) {
    throw "Murex flight screenshot 1 missing. See $log"
}
if (!$NullRhi -and !(Select-String -LiteralPath $log -Pattern 'Tracing Screenshot "MurexApproach" taken' -SimpleMatch)) {
    throw "Murex flight screenshot 2 missing. See $log"
}
if (Select-String -LiteralPath $log -Pattern 'Failed to compile Material|Fatal error') {
    throw "Murex rendering errors. See $log"
}
if (!(Select-String -LiteralPath $log -Pattern 'Log file closed' -SimpleMatch)) {
    throw "Murex process did not shut down orderly. See $log"
}
$flightPass = Select-String -LiteralPath $log -Pattern 'FlightTest PASS' -SimpleMatch
$flightFail = Select-String -LiteralPath $log -Pattern 'FlightTest FAIL' -SimpleMatch
if ($flightPass) { Write-Host 'FlightTest PASS' }
elseif ($flightFail) {
    throw "FlightTest FAIL. The level booted, but the flight test did not pass. See $log"
}
else { throw "Flight test did not report a result. See $log" }
Write-Host "Murex Spire Aerodrome check passed. Log: $log"
