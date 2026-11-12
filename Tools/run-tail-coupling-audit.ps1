param([switch]$Render)
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot
$artifacts = Join-Path $repo '.setup/tail-coupling-20261112'
$source = [IO.File]::ReadAllText((Join-Path $artifacts 'preferences-before.ini'))
$running = @()
foreach ($common in @(-25,0,25)) {
    foreach ($incidence in @(-2.3,2.3)) {
        $text = [regex]::Replace($source, '(?m)^GlideAngle=[^\r\n]*', ('GlideAngle='+(-$common)))
        $text = [regex]::Replace($text, '(?m)^TailElevatorAngle=[^\r\n]*', ('TailElevatorAngle='+$incidence))
        $preferences = Join-Path $artifacts "d$common-i$incidence.ini"
        [IO.File]::WriteAllText($preferences, $text)
        foreach ($version in @('before','candidate')) {
            $params = @{ Library=(Join-Path $artifacts "$version.dll");
                LogName="yaw-d$common-i$incidence-$version.log";
                ArtifactDirectory='.setup/tail-coupling-20261112'; Preferences=$preferences;
                Ramp=1; Roll=.15; YawFree=$true; Render=$Render }
            $result = & "$PSScriptRoot/run-wing-air-audit.ps1" @params
            Write-Output $result
            $running += [int]([regex]::Match($result,'PID=(\d+)').Groups[1].Value)
            if ($running.Count -ge 3) {
                foreach ($processId in $running) { Wait-Process -Id $processId -ErrorAction SilentlyContinue }
                $running = @()
            }
        }
    }
}
foreach ($processId in $running) { Wait-Process -Id $processId -ErrorAction SilentlyContinue }
