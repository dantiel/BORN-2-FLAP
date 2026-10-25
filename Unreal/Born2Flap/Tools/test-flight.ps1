param([string]$EngineRoot = 'V:\UE_5.8', [int[]]$FrameRates = @(30,60,144), [switch]$SkipSoak,
      [ValidateRange(-1,4)][int]$BirdModel = -1, [string]$Preferences = '',
      [ValidateRange(240,3840)][int]$PhysicsHz = 960)
$ErrorActionPreference = 'Stop'
$project = (Resolve-Path (Join-Path $PSScriptRoot '../Born2Flap.uproject')).Path
$logs = Join-Path (Split-Path $project) 'Saved/Logs'
New-Item -ItemType Directory -Path $logs -Force | Out-Null
$cases = @($FrameRates | ForEach-Object { @{ Name="flight-test-$_"; Fps=$_; Flag='B2FFlightTest'; Result='FlightTest PASS' } })
if (!$SkipSoak) { $cases += @{Name='flight-soak';Fps=60;Flag='B2FSoakTest';Result='FlightSoakTest PASS'} }
if ($BirdModel -lt 0) { $cases += @{Name='raven-flight-test';Fps=60;Flag='B2FRavenFlightTest';Result='RavenFlightTest PASS'} }
$cases += @{Name='handling-test';Fps=60;Flag='B2FHandlingTest';Result='HandlingTest PASS'}
$failures = @()
foreach ($case in $cases) {
    $log = Join-Path $logs ($case.Name+'.log')
    $map = if ($case.Flag -eq 'B2FRavenFlightTest') { '/Game/Ravenstonefield/Maps/RAVENSTONEFIELD' } else { '/Engine/Maps/Entry' }
    $arguments = @(('"'+$project+'"'),$map,'-game','-nullrhi','-nosound','-unattended','-nosplash','-benchmark',('-fps='+$case.Fps),('-'+$case.Flag),('-abslog="'+$log+'"'))
    $arguments += '-B2FPhysicsHz='+$PhysicsHz
    if ($BirdModel -ge 0) { $arguments += '-B2FTestBirdModel='+$BirdModel }
    if ($Preferences) {
        $preferencesPath = (Resolve-Path -LiteralPath $Preferences).Path
        $arguments += @('-B2FUseSavedFlightSettings', ('-B2FFlightPreferences="'+$preferencesPath+'"'))
    }
    $process = Start-Process -FilePath (Join-Path $EngineRoot 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe') -ArgumentList $arguments -WindowStyle Hidden -PassThru
    if (!$process.WaitForExit(180000)) {
        Stop-Process -Id $process.Id
        $failures += $case.Name
        Write-Warning ($case.Name+' timed out; see '+$log)
        continue
    }
    $result = Select-String -LiteralPath $log -Pattern $case.Result -SimpleMatch
    if ($process.ExitCode -ne 0 -or !$result) {
        $failures += $case.Name
        Write-Warning ($case.Name+' failed; see '+$log)
        continue
    }
    $result.Line
}
if ($failures.Count) { throw ('Failed flight checks: '+($failures -join ', ')) }
