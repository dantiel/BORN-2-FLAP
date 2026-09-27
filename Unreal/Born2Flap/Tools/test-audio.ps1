param([string]$EngineRoot = 'V:\UE_5.8')
$ErrorActionPreference = 'Stop'
$project = (Resolve-Path (Join-Path $PSScriptRoot '../Born2Flap.uproject')).Path
$log = Join-Path (Split-Path $project) 'Saved/Logs/aero-audio-test.log'
# A rendered game with the real audio device: deliberately no -nosound/-nullrhi.
$arguments = @(('"'+$project+'"'),'/Game/Ravenstonefield/Maps/RAVENSTONEFIELD','-game','-RenderOffscreen','-ResX=960','-ResY=540','-unattended','-nosplash','-B2FAudioTest','-B2FNoRacing',('-abslog="'+$log+'"'))
$process = Start-Process -FilePath (Join-Path $EngineRoot 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe') -ArgumentList $arguments -WindowStyle Hidden -PassThru
if (!$process.WaitForExit(180000)) { Stop-Process -Id $process.Id; throw "Audio test timed out: $log" }
$result = Select-String -LiteralPath $log -Pattern 'AeroAudioTest PASS' -SimpleMatch
if ($process.ExitCode -ne 0 -or !$result) { throw "Audio test failed: $log" }
$result.Line
