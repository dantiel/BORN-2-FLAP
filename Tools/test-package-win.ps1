# Exercise the cooked Shipping game, including its bundled native physics DLL.
param(
    [Parameter(Mandatory=$true)][string]$Package,
    [switch]$Render
)
$ErrorActionPreference = 'Stop'
$Package = (Resolve-Path -LiteralPath $Package).Path
$Exe = Join-Path $Package 'Born2Flap\Binaries\Win64\Born2Flap-Win64-Shipping.exe'
if (!(Test-Path -LiteralPath $Exe)) { throw "Packaged game missing: $Exe" }
$TestRoot = Join-Path (Split-Path $Package) ('smoke-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $TestRoot | Out-Null
$Results = @()
foreach ($Flag in @('B2FRavenFlightTest', 'B2FDesktopInputTest')) {
    $Arguments = @('/Game/Ravenstonefield/Maps/RAVENSTONEFIELD', '-unattended', '-nosplash',
        '-nosound', '-benchmark', '-fps=60', "-$Flag", ('-UserDir="' + $TestRoot + '"'))
    if ($Render) { $Arguments += @('-RenderOffscreen', '-ResX=1280', '-ResY=720') }
    else { $Arguments += '-nullrhi' }
    # Do not accidentally resolve DLLs from the build machine's GHC/Unreal PATH.
    $PreviousPath = $env:Path
    try {
        $env:Path = "$env:SystemRoot\System32;$env:SystemRoot"
        $Process = Start-Process -FilePath $Exe -ArgumentList $Arguments -WorkingDirectory $Package -WindowStyle Hidden -PassThru
    } finally { $env:Path = $PreviousPath }
    if (!$Process.WaitForExit(180000)) {
        Stop-Process -Id $Process.Id -Force
        throw "Packaged $Flag timed out. Test files: $TestRoot"
    }
    # Shipping compiles out UE_LOG; the in-game checks report failure via exit status.
    if ($Process.ExitCode -ne 0) { throw "Packaged $Flag failed: exit $($Process.ExitCode). Test files: $TestRoot" }
    $Results += @{test=$Flag;exitCode=$Process.ExitCode;rendered=[bool]$Render}
    Write-Host "Packaged $Flag PASS (Shipping exit status 0)"
}
$Results | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $TestRoot 'results.json') -Encoding UTF8
