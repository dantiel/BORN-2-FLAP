param(
    [string]$EngineRoot='V:\UE_5.8',
    [ValidateSet('Shiomori','Ravenstonefield','Training')][string]$Level='Shiomori',
    [string]$Weather='parhelion', [string]$DayTime='morning',
    [switch]$All, [switch]$Menu, [switch]$Experience, [switch]$BirdWater, [switch]$Volcanic
)
$ErrorActionPreference='Stop'
if($BirdWater){$Experience=$true;$Level='Shiomori'}
if($Volcanic){$Experience=$true;$Level='Shiomori'}
$project=(Resolve-Path (Join-Path $PSScriptRoot '../Born2Flap.uproject')).Path
$cases=, @($Level,$Weather,$DayTime)
if($All){$cases=@(
    @('Shiomori','rain','noon'), @('Shiomori','cloudy','sunset'),
    @('Shiomori','mist','dawn'), @('Ravenstonefield','snow','morning'),
    @('Shiomori','parhelion','morning'), @('Shiomori','sunny','night'),
    @('Training','sunny','noon'))}
if($All -and $Experience){$cases=@(@('Shiomori','sunny','morning'),@('Shiomori','rain','noon'),@('Ravenstonefield','snow','noon'),@('Training','rain','noon'),@('Ravenstonefield','mist','night'))}
foreach($case in $cases){
    $levelId,$weatherId,$timeId=$case
    $map=switch($levelId){'Shiomori'{'/Game/Shiomori/Maps/SHIOMORI'} 'Ravenstonefield'{'/Game/Ravenstonefield/Maps/RAVENSTONEFIELD'} default{'/Engine/Maps/Entry'}}
    $log=Join-Path (Split-Path $project) "Saved/Logs/weather-$levelId-$weatherId-$timeId.log"
    $mode=if($Menu){'-B2FWeatherMenuTest'}elseif($Experience){'-B2FWeatherExperienceTest'}else{'-B2FWeatherTest'}
    $arguments=@(('"'+$project+'"'),$map,'-game','-nosplash','-unattended','-benchmark','-fps=60',
        '-RenderOffscreen','-ResX=1280','-ResY=720',"-B2FLevel=$levelId","-B2FWeather=$weatherId","-B2FDayTime=$timeId",$mode,('-abslog="'+$log+'"'))
    if(!$Experience){$arguments+='-nosound'}
    else{$arguments=@($arguments | Where-Object {$_ -ne '-benchmark'});$arguments+=@('-sound','-AudioMixer')}
    if($BirdWater){$arguments+='-B2FBirdWaterTest'}
    if($Volcanic){$arguments+='-B2FVolcanicTest'}
    $process=Start-Process -FilePath (Join-Path $EngineRoot 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe') -ArgumentList $arguments -WindowStyle Hidden -PassThru
    if(!$process.WaitForExit(180000)){Stop-Process -Id $process.Id;throw "Weather test timed out: $log"}
    $marker=if($Menu){'WeatherMenuTest PASS'}elseif($Experience){'WeatherExperienceTest PASS'}else{'WeatherTest PASS'}
    if($process.ExitCode -ne 0 -or !(Select-String -LiteralPath $log -Pattern $marker -SimpleMatch)){throw "Weather test failed: $log"}
    if($Menu -and !(Select-String -LiteralPath $log -Pattern 'WeatherMenuTravel PASS' -SimpleMatch)){throw "Menu travel failed: $log"}
    if(Select-String -LiteralPath $log -Pattern 'Failed to compile Material|LogShaderCompilers: Error|Weather.*Test FAIL'){throw "Weather render failed: $log"}
    if($Experience){
        $capture=Join-Path (Split-Path $project) "Saved/BouncedWavFiles/EXPERIENCE_$($levelId)_$($weatherId).wav"
        python (Join-Path $PSScriptRoot 'check_soundscape_audio.py') $capture
        if($LASTEXITCODE -ne 0){throw "Soundscape capture failed: $capture"}
    }
    Write-Output "$levelId / $weatherId / $timeId : $marker"
}
