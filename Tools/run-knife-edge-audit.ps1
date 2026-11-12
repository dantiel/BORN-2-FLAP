param([string]$Library = '.setup/knife-edge-20261112/before.dll', [string]$Tag = '', [double]$Speed = 8, [double]$Sink = 2, [switch]$Recover)
$ErrorActionPreference='Stop'
$repo=Split-Path $PSScriptRoot
$out=Join-Path $repo '.setup/knife-edge-20261112'
$source=[IO.File]::ReadAllText((Join-Path $out 'preferences-before.ini'))
$running=@()
foreach ($common in @(-25,0,25)) {
  foreach ($tail in @(-2.3,-8.4)) {
    $text=[regex]::Replace($source,'(?m)^GlideAngle=[^\r\n]*',('GlideAngle='+(-$common)))
    $text=[regex]::Replace($text,'(?m)^TailElevatorAngle=[^\r\n]*',('TailElevatorAngle='+$tail))
    $text=[regex]::Replace($text,'(?m)^CgOffsetMm=[^\r\n]*','CgOffsetMm=0')
    $prefs=Join-Path $out "d$common-tail$tail.ini"
    [IO.File]::WriteAllText($prefs,$text)
    $rolls=if ($Recover) {@(-.15)} else {@(0,.15)}
    foreach ($roll in $rolls) {
      $r=& "$PSScriptRoot/run-wing-air-audit.ps1" -Library $Library -LogName "d$common-tail$tail-roll$roll$Tag.log" `
        -ArtifactDirectory '.setup/knife-edge-20261112' -Preferences $prefs -InitialBank 80 -InitialSink $Sink -Speed $Speed -Roll $roll -Ramp .25
      Write-Output $r
      $running += [int]([regex]::Match($r,'PID=(\d+)').Groups[1].Value)
      if ($running.Count -eq 3) {
        foreach ($id in $running) { Wait-Process -Id $id -ErrorAction SilentlyContinue }
        $running=@()
      }
    }
  }
}
foreach ($id in $running) { Wait-Process -Id $id -ErrorAction SilentlyContinue }
