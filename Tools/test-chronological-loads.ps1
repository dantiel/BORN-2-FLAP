param([string]$Library = 'Unreal/Born2Flap/Binaries/ThirdParty/born2flap_math.dll')
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot
foreach ($fps in @(30,60,120)) {
    $name = "chronology-regression-$fps.log"
    $result = & "$PSScriptRoot/run-wing-air-audit.ps1" -Library $Library -LogName $name `
        -SavedSettings -ChronologyAudit -Fps $fps
    $processId = [int]([regex]::Match($result,'PID=(\d+)').Groups[1].Value)
    $process = Get-Process -Id $processId
    if (!$process.WaitForExit(60000)) { Stop-Process -Id $processId; throw "Torque-pulse test timed out at $fps fps" }
    $path = Join-Path $root ('.setup/wing-air-20261111/'+$name)
    $pass = Select-String -LiteralPath $path -Pattern 'ChronologyAudit PASS' -SimpleMatch
    if (!$pass) { throw "Torque-pulse regression failed at $fps fps; see $path" }
    $pass.Line
}
