param([string]$EngineRoot = 'V:\UE_5.8',
      [string]$Preferences = 'Unreal/Born2Flap/Saved/Config/FlightPreferences.ini',
      [ValidateRange(240,3840)][int]$PhysicsHz = 240,
      [switch]$Glide, [ValidateRange(0,3)][double]$RampSeconds = 0,
      [string]$LogPrefix = 'yaw-bank')
$ErrorActionPreference = 'Stop'
$project = (Resolve-Path "$PSScriptRoot/../Unreal/Born2Flap/Born2Flap.uproject").Path
$profile = (Resolve-Path -LiteralPath $Preferences).Path
$logs = Join-Path (Split-Path $project) 'Saved/Logs'
$failed = @()
foreach ($direction in @(1,-1)) {
    $log = Join-Path $logs ("$LogPrefix-$direction.log")
    $arguments = @(('"'+$project+'"'), '/Engine/Maps/Entry', '-game', '-nullrhi',
        '-unattended', '-nosplash', '-nosound', '-benchmark', '-fps=60',
        '-B2FFlightTest', '-B2FTestBirdModel=2', '-B2FUseSavedFlightSettings',
        ('-B2FYawBankTest='+$direction), ('-B2FPhysicsHz='+$PhysicsHz),
        ('-B2FFlightPreferences="'+$profile+'"'), ('-abslog="'+$log+'"'))
    $arguments += '-B2FYawTestRamp='+$RampSeconds.ToString([Globalization.CultureInfo]::InvariantCulture)
    if ($Glide) { $arguments += '-B2FYawTestGlide' }
    $process = Start-Process -FilePath (Join-Path $EngineRoot 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe') -ArgumentList $arguments -WindowStyle Hidden -PassThru
    if (!$process.WaitForExit(180000)) {
        Stop-Process -Id $process.Id
        $failed += $direction
        Write-Warning "Yaw test $direction timed out: $log"
        continue
    }
    $result = Select-String -LiteralPath $log -Pattern 'YawBankTest PASS' -SimpleMatch
    if ($process.ExitCode -ne 0 -or !$result) {
        $failed += $direction
        Select-String -LiteralPath $log -Pattern 'YawBankTest FAIL' -SimpleMatch | ForEach-Object { $_.Line }
        Write-Warning "Yaw test $direction failed: $log"
    } else { $result.Line }
}
if ($failed.Count) { throw ('Yaw-bank tests failed: '+($failed -join ', ')) }
