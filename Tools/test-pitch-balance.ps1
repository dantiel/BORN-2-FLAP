param([string]$EngineRoot='V:\UE_5.8',
      [string]$Preferences='Unreal/Born2Flap/Saved/Config/FlightPreferences.ini',
      [string]$Library='', [double[]]$CgMm=@(-100,0,100), [switch]$LaunchVelocity, [switch]$CgSlider,
      [ValidateRange(0,1)][double]$Wheel=.5,
      [ValidateRange(0,1)][double]$Throttle=.72,
      [ValidateRange(-1,1)][double]$Pitch=0,
      [ValidateRange(30,120)][int]$FrameRate=60,
      [ValidateRange(240,3840)][int]$PhysicsHz=240,
      [string]$LogPrefix='pitch-balance')
$ErrorActionPreference='Stop'
$project=(Resolve-Path "$PSScriptRoot/../Unreal/Born2Flap/Born2Flap.uproject").Path
$profile=(Resolve-Path -LiteralPath $Preferences).Path
$logs=Join-Path (Split-Path $project) 'Saved/Logs'
$culture=[Globalization.CultureInfo]::InvariantCulture
$failed=@()
foreach($cg in $CgMm) {
    $log=Join-Path $logs "$LogPrefix-$cg.log"
    $arguments=@(('"'+$project+'"'),'/Engine/Maps/Entry','-game','-nullrhi',
        '-unattended','-nosplash','-nosound','-benchmark',('-fps='+$FrameRate),
        '-B2FFlightTest','-B2FTestBirdModel=2','-B2FUseSavedFlightSettings',
        '-B2FPitchBalanceTest',('-B2FPitchTestCg='+$cg.ToString($culture)),
        ('-B2FTestWheel='+$Wheel.ToString($culture)),('-B2FPhysicsHz='+$PhysicsHz),
        ('-B2FPitchTestThrottle='+$Throttle.ToString($culture)),('-B2FPitchTestInput='+$Pitch.ToString($culture)),
        ('-B2FFlightPreferences="'+$profile+'"'),('-abslog="'+$log+'"'))
    if($Library) { $arguments+=('-B2FPhysicsTestLibrary="'+(Resolve-Path -LiteralPath $Library).Path+'"') }
    if($LaunchVelocity) { $arguments+='-B2FPitchTestLaunch' }
    if($CgSlider) { $arguments+='-B2FCgSliderTest' }
    $process=Start-Process -FilePath (Join-Path $EngineRoot 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe') -ArgumentList $arguments -WindowStyle Hidden -PassThru
    if(!$process.WaitForExit(180000)) { Stop-Process -Id $process.Id; throw "Pitch test timed out: $log" }
    Select-String -LiteralPath $log -Pattern 'PitchBalanceTest (PASS|FAIL)' | ForEach-Object Line
    if($CgSlider) {
        Select-String -LiteralPath $log -Pattern 'Cg(Slider|Action)Test (PASS|FAIL)' | ForEach-Object Line
        if(!(Select-String -LiteralPath $log -SimpleMatch 'CgActionTest PASS native=1 brain=1')) { $failed+=$cg }
    }
    if($process.ExitCode -ne 0 -or !(Select-String -LiteralPath $log -SimpleMatch 'PitchBalanceTest PASS')) { $failed+=$cg }
}
if($failed.Count) { throw ('Pitch balance failed at CG mm: '+($failed -join ', ')) }
