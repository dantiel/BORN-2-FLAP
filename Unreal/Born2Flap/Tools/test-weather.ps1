param(
    [string]$EngineRoot='V:\UE_5.8',
    [ValidateSet('Shiomori','Ravenstonefield','Training')][string]$Level='Shiomori',
    [string]$Weather='parhelion', [string]$DayTime='morning',
    [switch]$All, [switch]$Menu
)
$ErrorActionPreference='Stop'
$project=(Resolve-Path (Join-Path $PSScriptRoot '../Born2Flap.uproject')).Path
$cases=, @($Level,$Weather,$DayTime)
if($All){$cases=@(
    @('Shiomori','rain','noon'), @('Shiomori','cloudy','sunset'),
    @('Shiomori','mist','dawn'), @('Ravenstonefield','snow','morning'),
    @('Shiomori','parhelion','morning'), @('Shiomori','sunny','night'),
    @('Training','sunny','noon'))}
foreach($case in $cases){
    $levelId,$weatherId,$timeId=$case
    $map=switch($levelId){'Shiomori'{'/Game/Shiomori/Maps/SHIOMORI'} 'Ravenstonefield'{'/Game/Ravenstonefield/Maps/RAVENSTONEFIELD'} default{'/Engine/Maps/Entry'}}
    $log=Join-Path (Split-Path $project) "Saved/Logs/weather-$levelId-$weatherId-$timeId.log"
    $mode=if($Menu){'-B2FWeatherMenuTest'}else{'-B2FWeatherTest'}
    $arguments=@(('"'+$project+'"'),$map,'-game','-nosound','-nosplash','-unattended','-benchmark','-fps=60',
        '-RenderOffscreen','-ResX=1280','-ResY=720',"-B2FLevel=$levelId","-B2FWeather=$weatherId","-B2FDayTime=$timeId",$mode,('-abslog="'+$log+'"'))
    $process=Start-Process -FilePath (Join-Path $EngineRoot 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe') -ArgumentList $arguments -WindowStyle Hidden -PassThru
    if(!$process.WaitForExit(180000)){Stop-Process -Id $process.Id;throw "Weather test timed out: $log"}
    $marker=if($Menu){'WeatherMenuTest PASS'}else{'WeatherTest PASS'}
    if($process.ExitCode -ne 0 -or !(Select-String -LiteralPath $log -Pattern $marker -SimpleMatch)){throw "Weather test failed: $log"}
    if($Menu -and !(Select-String -LiteralPath $log -Pattern 'WeatherMenuTravel PASS' -SimpleMatch)){throw "Menu travel failed: $log"}
    if(Select-String -LiteralPath $log -Pattern 'Failed to compile Material|LogShaderCompilers: Error|WeatherTest FAIL'){throw "Weather render failed: $log"}
    Write-Output "$levelId / $weatherId / $timeId : $marker"
}
