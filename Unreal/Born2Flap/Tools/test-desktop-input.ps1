param([string]$EngineRoot = 'V:\UE_5.8', [switch]$Capture, [switch]$BirdPreview, [switch]$SettingsCapture, [switch]$WingPaintPreview)
$ErrorActionPreference = 'Stop'
$project = (Resolve-Path (Join-Path $PSScriptRoot '../Born2Flap.uproject')).Path
$log = Join-Path (Split-Path $project) 'Saved/Logs/desktop-input-test.log'
$arguments = @(('"'+$project+'"'),'/Game/Ravenstonefield/Maps/RAVENSTONEFIELD','-game','-nosound','-unattended','-nosplash','-benchmark','-fps=60','-B2FDesktopInputTest',('-abslog="'+$log+'"'))
if ($Capture) { $arguments += @('-RenderOffscreen','-ResX=1600','-ResY=900','-B2FChannelCapture') }
else { $arguments += '-nullrhi' }
if ($WingPaintPreview) { $arguments += '-B2FWingPaintPreview' }
if ($BirdPreview) { $arguments += '-B2FBirdPreview' }
if ($SettingsCapture) { $arguments += '-B2FSettingsCapture' }
$arguments += '-B2FNoMenu'
$process = Start-Process -FilePath (Join-Path $EngineRoot 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe') -ArgumentList $arguments -WindowStyle Hidden -PassThru
if (!$process.WaitForExit(180000)) { Stop-Process -Id $process.Id; throw "Desktop input test timed out: $log" }
$result = Select-String -LiteralPath $log -Pattern 'DesktopInputTest PASS' -SimpleMatch
if ($process.ExitCode -ne 0 -or !$result) { throw "Desktop input test failed: $log" }
$result.Line
