param([string]$EngineRoot='V:\UE_5.8',[switch]$NoRender,[switch]$Sky)
$ErrorActionPreference='Stop'
$project=(Resolve-Path (Join-Path $PSScriptRoot '../Born2Flap.uproject')).Path
$log=Join-Path (Split-Path $project) 'Saved/Logs/shiomori-test.log'
$arguments=@(('"'+$project+'"'),'/Game/Shiomori/Maps/SHIOMORI','-game','-B2FCoastTest','-B2FLevel=Shiomori','-nosound','-unattended','-nosplash','-benchmark','-fps=60',('-abslog="'+$log+'"'))
if($NoRender){$arguments+='-nullrhi'}else{$arguments+=@('-RenderOffscreen','-ResX=1600','-ResY=900')}
if($Sky){
    if($NoRender){throw 'Sky validation requires rendering.'}
    $arguments+='-B2FSkyTest'
}
$process=Start-Process -FilePath (Join-Path $EngineRoot 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe') -ArgumentList $arguments -WindowStyle Hidden -PassThru
if(!$process.WaitForExit(180000)){Stop-Process -Id $process.Id;throw 'Shiomori test timed out'}
$result=Select-String -LiteralPath $log -Pattern 'ShiomoriTest PASS' -SimpleMatch
if($process.ExitCode -ne 0 -or !$result){throw "Shiomori test failed: $log"}
if($Sky -and (Select-String -LiteralPath $log -Pattern 'Failed to compile Material|LogShaderCompilers: Error')){throw "Sky shader compilation failed: $log"}
$result.Line
