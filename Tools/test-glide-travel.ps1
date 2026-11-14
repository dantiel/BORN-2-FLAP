param([string]$ArtifactDirectory='.setup/tail-travel-20261113', [switch]$Regressions, [switch]$Combined)
$ErrorActionPreference='Stop'
$jobs=@()
if($Regressions) {
    foreach($test in @('B2FHandlingTest','B2FFlightTest','B2FSoakTest')) {
        foreach($version in @('before','candidate')) {
            $jobs+=@{name="$test-$version";version=$version;test=$test;common=$null}
        }
    }
} elseif($Combined) {
    foreach($bank in @(0,80,-80)) {foreach($version in @('before','candidate')) {
        $jobs+=@{name="combined-bank$bank-$version";version=$version;bank=$bank;common=$null;ramp=.25}
    }}
} else {
    foreach($common in @(-15,15)) {foreach($ramp in @(.25,8)) {foreach($version in @('before','candidate')) {
        $jobs+=@{name="tow-c$common-r$ramp-$version";common=$common;ramp=$ramp;version=$version;bank=$null}
    }}}
    foreach($common in @(-15,0)) {foreach($version in @('before','candidate')) {
        $jobs+=@{name="free-c$common-$version";common=$common;ramp=.25;version=$version;bank=80}
    }}
}
$active=@()
foreach($job in $jobs) {
    $prefs=if($null -eq $job.common){'preferences.ini'}else{"common$($job.common).ini"}
    $params=@{Library="$ArtifactDirectory/$($job.version).dll";LogName="$($job.name).log";
        ArtifactDirectory=$ArtifactDirectory;Preferences="$ArtifactDirectory/$prefs"}
    if($Regressions){$params.Test=$job.test}
    else {
        $params.Ramp=$job.ramp;$params.Roll=-.65
        if($null -ne $job.bank){$params.InitialBank=$job.bank;$params.InitialSink=2}
        if($Combined) {
            $params.Roll=if($job.bank -lt 0){.78}else{-.78}
            $params.Yaw=$params.Roll;$params.Pitch=1;$params.InputDuration=1.5;$params.TailControlTrace=$true
        }
    }
    $result=& "$PSScriptRoot/run-wing-air-audit.ps1" @params
    Write-Output $result
    $active+=[int]([regex]::Match($result,'PID=(\d+)').Groups[1].Value)
    if($active.Count -eq 3){
        foreach($auditId in $active){Wait-Process -Id $auditId -ErrorAction SilentlyContinue}
        $active=@()
    }
}
foreach($auditId in $active){Wait-Process -Id $auditId -ErrorAction SilentlyContinue}
