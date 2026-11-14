param([string]$ArtifactDirectory='.setup/knife-upstroke-20261113', [switch]$Regressions,
      [string[]]$Versions=@('before','candidate'))
$ErrorActionPreference='Stop'
$active=@()
if($Regressions){
    foreach($test in @('B2FHandlingTest','B2FFlightTest','B2FSoakTest')){foreach($version in $Versions){
        $result=& "$PSScriptRoot/run-wing-air-audit.ps1" -Library "$ArtifactDirectory/servo-$version.dll" `
            -LogName "$test-$version.log" -Test $test -ArtifactDirectory $ArtifactDirectory `
            -Preferences "$ArtifactDirectory/preferences.ini"
        Write-Output $result
        $active+=[int]([regex]::Match($result,'PID=(\d+)').Groups[1].Value)
        if($active.Count -eq 3){
            foreach($auditId in $active){Wait-Process -Id $auditId -ErrorAction SilentlyContinue}
            $active=@()
        }
    }}
    foreach($auditId in $active){Wait-Process -Id $auditId -ErrorAction SilentlyContinue}
    return
}
foreach($speed in @(2,8)) {foreach($yaw in @(-.78,0,.78)) {foreach($version in $Versions) {
    $result=& "$PSScriptRoot/run-wing-air-audit.ps1" -Library "$ArtifactDirectory/servo-$version.dll" `
        -LogName "powered-U$speed-yaw$yaw-$version.log" -ArtifactDirectory $ArtifactDirectory `
        -Preferences "$ArtifactDirectory/preferences.ini" -InitialBank 80 -InitialSink 2 `
        -Speed $speed -Throttle .8 -Roll 0 -Pitch 0 -Yaw $yaw -Ramp .25 -InputDuration 2 -TailControlTrace
    Write-Output $result
    $active+=[int]([regex]::Match($result,'PID=(\d+)').Groups[1].Value)
    if($active.Count -eq 3){
        foreach($auditId in $active){Wait-Process -Id $auditId -ErrorAction SilentlyContinue}
        $active=@()
    }
}}}
foreach($auditId in $active){Wait-Process -Id $auditId -ErrorAction SilentlyContinue}
