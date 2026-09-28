# Wrap an already cooked Windows package as one self-extracting download.
param(
    [Parameter(Mandatory=$true)][string]$Package,
    [Parameter(Mandatory=$true)][string]$OutDir,
    [string]$Tag = 'dev',
    [string]$SevenZip = 'C:\Program Files\7-Zip\7z.exe'
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if ($Tag -notmatch '^[A-Za-z0-9][A-Za-z0-9._-]*$') { throw 'Invalid version.' }
$Package = (Resolve-Path -LiteralPath $Package).Path
$OutDir = [IO.Path]::GetFullPath($OutDir)
$Sfx = Join-Path (Split-Path $SevenZip) '7z.sfx'
$License = Join-Path (Split-Path $SevenZip) 'License.txt'
foreach ($Required in @($SevenZip, $Sfx, $License, (Join-Path $Package 'Born2Flap.exe'))) {
    if (!(Test-Path -LiteralPath $Required)) { throw "Missing packaging input: $Required" }
}
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$Output = Join-Path $OutDir "BORN2FLAP-$Tag-win64-extract.exe"
if (Test-Path -LiteralPath $Output) { throw "Output already exists: $Output" }
# Include the unmodified extractor's redistribution notice and source location.
Copy-Item -LiteralPath $License -Destination (Join-Path $Package '7-Zip-LICENSE.txt')
@"
This download uses the unmodified 7-Zip self-extractor, licensed under GNU LGPL.
7-Zip version: $((Get-Item -LiteralPath $SevenZip).VersionInfo.ProductVersion)
Source code and license information: https://www.7-zip.org/
Double-click the download, select an empty destination folder and extract.
Then open Born2Flap.exe in that folder. Keep all extracted files together.
This is a portable game, not an installer; no administrator access is required.
"@ | Set-Content -LiteralPath (Join-Path $Package 'EXTRACT-README.txt') -Encoding UTF8
Push-Location $Package
try {
    & $SevenZip a -t7z "-sfx$Sfx" -mx=5 -mmt=4 $Output '.\*'
    if ($LASTEXITCODE) { throw "Self-extracting package creation failed: $LASTEXITCODE" }
} finally { Pop-Location }
& $SevenZip t $Output
if ($LASTEXITCODE) { throw 'Self-extracting archive integrity test failed.' }
$Hash = (Get-FileHash -LiteralPath $Output -Algorithm SHA256).Hash.ToLowerInvariant()
"$Hash  $([IO.Path]::GetFileName($Output))" | Set-Content -LiteralPath "$Output.sha256" -Encoding ASCII
Write-Host "Self-extracting package ready: $Output"
