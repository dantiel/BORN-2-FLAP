param([string]$Library = '.setup/wing-air-20261111/current.dll')
$ErrorActionPreference = 'Stop'
$cases = @(
    @{Name='glide-slow'; Ramp=8; Speed=8},
    @{Name='glide-sharp'; Ramp=.1; Speed=8},
    @{Name='low-slow'; Ramp=8; Speed=2},
    @{Name='low-middle'; Ramp=1; Speed=2},
    @{Name='low-sharp'; Ramp=.1; Speed=2},
    @{Name='powered-slow'; Ramp=8; Speed=8; Throttle=.6},
    @{Name='powered-middle'; Ramp=1; Speed=8; Throttle=.6},
    @{Name='powered-sharp'; Ramp=.1; Speed=8; Throttle=.6},
    @{Name='fps30'; Ramp=1; Fps=30},
    @{Name='fps120'; Ramp=1; Fps=120},
    @{Name='fps45'; Ramp=1; Fps=45},
    @{Name='free-middle'; Ramp=1; FreeFlight=$true}
)
$running = @()
foreach ($case in $cases) {
    foreach ($average in @($true,$false)) {
        $tag = if ($average) {'before'} else {'after'}
        $params = @{Library=$Library; LogName=('matrix-'+$case.Name+'-'+$tag+'.log');
                    SavedSettings=$true; PhysicsHz=240; AverageLoads=$average}
        foreach ($key in $case.Keys) { if ($key -ne 'Name') { $params[$key]=$case[$key] } }
        $result = & "$PSScriptRoot/run-wing-air-audit.ps1" @params
        Write-Output $result
        $running += [int]([regex]::Match($result,'PID=(\d+)').Groups[1].Value)
        if ($running.Count -ge 3) {
            foreach ($processId in $running) { Wait-Process -Id $processId -ErrorAction SilentlyContinue }
            $running = @()
        }
    }
}
foreach ($processId in $running) { Wait-Process -Id $processId -ErrorAction SilentlyContinue }
