param([string]$EngineRoot='V:\UE_5.8',[switch]$Capture)
$ErrorActionPreference='Stop'
$project=(Resolve-Path (Join-Path $PSScriptRoot '../Born2Flap.uproject')).Path
$log=Join-Path (Split-Path $project) 'Saved/Logs/harvest-validation.log'
$arguments=@(('"'+$project+'"'),'/Game/Shiomori/Maps/SHIOMORI','-game','-B2FLevel=Shiomori','-B2FActivity=HarvestWatch','-B2FWeather=sunny','-B2FDayTime=noon','-nosound','-unattended','-nosplash',('-abslog="'+$log+'"'))
if($Capture){$arguments+=@('-B2FHarvestCapture','-RenderOffscreen','-ResX=1600','-ResY=900')}
else{$arguments+=@('-B2FHarvestTest','-nullrhi')}
$p=Start-Process -FilePath (Join-Path $EngineRoot 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe') -ArgumentList $arguments -WindowStyle Hidden -PassThru
if(!$p.WaitForExit(180000)){Stop-Process -Id $p.Id;throw "Harvest Watch timed out: $log"}
$marker=if($Capture){'HarvestWatchCapture PASS'}else{'HarvestWatchTest PASS'}
if($p.ExitCode -ne 0 -or !(Select-String -LiteralPath $log -SimpleMatch $marker)){throw "Harvest Watch failed: $log"}
if(Select-String -LiteralPath $log -Pattern 'Failed to compile Material|LogShaderCompilers: Error|Fatal error:'){throw "Render error: $log"}
$marker
