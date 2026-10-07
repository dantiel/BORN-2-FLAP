param([int]$TimeoutSeconds = 1800)
$ErrorActionPreference = 'Continue'
& (Join-Path $PSScriptRoot 'build.ps1') *>&1 | Tee-Object -FilePath (Join-Path $PSScriptRoot '..\..\..\build-out.log')
"BUILD_EXIT=$LASTEXITCODE"
exit $LASTEXITCODE
