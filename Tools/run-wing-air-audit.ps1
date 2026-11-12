param(
    [Parameter(Mandatory=$true)][string]$Library,
    [Parameter(Mandatory=$true)][string]$LogName,
    [int]$PhysicsHz = 240,
    [string]$Test = 'B2FRollResponseTest',
    [switch]$SavedSettings,
    [double]$Ramp = 8,
    [double]$Speed = 8,
    [double]$Throttle = 0,
    [switch]$FreeFlight,
    [switch]$InertiaAudit,
    [switch]$AverageLoads,
    [switch]$ChronologyAudit,
    [switch]$ChronologyTrace,
    [switch]$Render,
    [switch]$Installed,
    [double]$Roll = .25,
    [int]$Fps = 60,
    [int]$BirdModel = 2,
    [string]$ArtifactDirectory = '.setup/wing-air-20261111',
    [string]$Preferences = '',
    [switch]$YawFree,
    [Nullable[double]]$InitialBank = $null,
    [double]$InitialSink = 0
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot
$dll = (Resolve-Path -LiteralPath $Library).Path
$log = Join-Path (Join-Path $repo $ArtifactDirectory) $LogName
$project = Join-Path $repo 'Unreal/Born2Flap/Born2Flap.uproject'
$arguments = @(('"'+$project+'"'), '/Engine/Maps/Entry', '-game', '-nullrhi', '-nosound',
    '-unattended', '-nosplash', '-benchmark', ('-fps='+$Fps), '-B2FFlightTest',
    ('-B2FTestBirdModel='+$BirdModel), ('-'+$Test), ('-B2FPhysicsHz='+$PhysicsHz),
    ('-abslog="'+$log+'"'))
if (!$Installed) { $arguments += ('-B2FPhysicsTestLibrary="'+$dll+'"') }
$arguments += @('-B2FWingAirTrace', ('-B2FAuditRamp='+$Ramp),
    ('-B2FAuditSpeed='+$Speed), ('-B2FAuditThrottle='+$Throttle))
if ($FreeFlight) { $arguments += '-B2FAuditFree' }
if ($YawFree) { $arguments += '-B2FAuditYawFree' }
if ($null -ne $InitialBank) { $arguments += @(('-B2FAuditInitialBank='+$InitialBank),('-B2FAuditInitialSink='+$InitialSink)) }
if ($InertiaAudit) { $arguments += '-B2FInertiaAudit' }
if ($AverageLoads) { $arguments += '-B2FAuditAverageLoads' }
if ($ChronologyAudit) {
    # This deliberately tiny, zero-translation torque fixture must remain
    # awake; otherwise Chaos's normal sleep threshold suppresses its motion.
    $arguments += @('-B2FChronologyAudit','-ExecCmds="p.Chaos.Solver.Sleep.Enabled 0"')
}
if ($ChronologyTrace) { $arguments += '-B2FChronologyTrace' }
$arguments += ('-B2FAuditRoll='+$Roll)
if ($Render) {
    $arguments = @($arguments | Where-Object { $_ -ne '-nullrhi' })
    $prefix = Join-Path (Join-Path $repo $ArtifactDirectory) ([IO.Path]::GetFileNameWithoutExtension($LogName))
    $arguments += @('-RenderOffscreen','-ResX=1280','-ResY=720',('-B2FAuditCapturePrefix="'+$prefix+'"'))
}
if ($SavedSettings -or $Preferences) {
    if (!$Preferences) { $Preferences = Join-Path $repo '.setup/wing-air-20261111/preferences.ini' }
    if (!(Test-Path -LiteralPath $preferences)) {
        Copy-Item -LiteralPath (Join-Path $repo 'Unreal/Born2Flap/Saved/Config/FlightPreferences.ini') -Destination $preferences
    }
    $preferences = (Resolve-Path -LiteralPath $preferences).Path
    $arguments += @('-B2FUseSavedFlightSettings', ('-B2FFlightPreferences="'+$preferences+'"'))
}
$process = Start-Process -FilePath 'V:/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe' `
    -ArgumentList $arguments -WindowStyle Hidden -PassThru
Write-Output ('PID=' + $process.Id + ' log=' + $log)
