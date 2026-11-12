$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot
$artifacts = Join-Path $repo '.setup/tail-coupling-20261112'
$running = @()
foreach ($test in @('B2FHandlingTest','B2FFlightTest','B2FSoakTest')) {
    foreach ($version in @('before','candidate')) {
        $result = & "$PSScriptRoot/run-wing-air-audit.ps1" -Library (Join-Path $artifacts "$version.dll") `
            -LogName "$test-$version.log" -Test $test -ArtifactDirectory '.setup/tail-coupling-20261112' `
            -Preferences (Join-Path $artifacts 'preferences-before.ini')
        Write-Output $result
        $running += [int]([regex]::Match($result,'PID=(\d+)').Groups[1].Value)
        if ($running.Count -ge 3) {
            foreach ($processId in $running) { Wait-Process -Id $processId -ErrorAction SilentlyContinue }
            $running = @()
        }
    }
}
foreach ($processId in $running) { Wait-Process -Id $processId -ErrorAction SilentlyContinue }
