param([string]$ArtifactDirectory='.setup/knife-control-20261113b',
      [string[]]$Versions=@('other-agent-installed','pressure-moment'),
      [double[]]$Speeds=@(2,8),[switch]$Combined,[switch]$Coordinated,[switch]$Regressions)
$ErrorActionPreference='Stop'
$active=@()
function WaitBatch {
    foreach($auditId in $script:active){Wait-Process -Id $auditId -ErrorAction SilentlyContinue}
    $script:active=@()
}
if ($Regressions) {
    foreach($test in @('B2FHandlingTest','B2FFlightTest','B2FSoakTest')) { foreach($version in $Versions) {
        $result=& "$PSScriptRoot/run-wing-air-audit.ps1" -Library "$ArtifactDirectory/$version.dll" `
            -LogName "$test-$version.log" -Test $test -ArtifactDirectory $ArtifactDirectory `
            -Preferences "$ArtifactDirectory/preferences.ini"
        Write-Output $result
        $active+=[int]([regex]::Match($result,'PID=(\d+)').Groups[1].Value)
        if($active.Count -eq 3){WaitBatch}
    }}
    WaitBatch
    return
}
$controls=if($Coordinated){@(@(.5,-.5,.78),@(-.5,-.5,-.78))}elseif($Combined){@(@(-.5,-.5,.78),@(.5,-.5,-.78),@(0,-.5,.78))}else{@(@(0,0,-.78),@(0,0,0),@(0,0,.78))}
foreach($speed in $Speeds){foreach($control in $controls){foreach($version in $Versions){
    $roll,$pitch,$yaw=$control
    $result=& "$PSScriptRoot/run-wing-air-audit.ps1" -Library "$ArtifactDirectory/$version.dll" `
        -LogName "powered-U$speed-r$roll-p$pitch-y$yaw-$version.log" -ArtifactDirectory $ArtifactDirectory `
        -Preferences "$ArtifactDirectory/preferences.ini" -InitialBank 80 -InitialSink 2 `
        -Speed $speed -Throttle .8 -Roll $roll -Pitch $pitch -Yaw $yaw -Ramp .25 -InputDuration 2 -TailControlTrace
    Write-Output $result
    $active+=[int]([regex]::Match($result,'PID=(\d+)').Groups[1].Value)
    if($active.Count -eq 3){WaitBatch}
}}}
WaitBatch
