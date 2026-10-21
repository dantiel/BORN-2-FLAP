# fetch_portable_ruby.ps1 — vendor a self-contained Ruby into ThirdParty/ruby/
# so the UMGHAML Brain runs without a system-wide Ruby install.
#
#   powershell -NoProfile -ExecutionPolicy Bypass -File Tools/fetch_portable_ruby.ps1
#
# Downloads the base RubyInstaller 7z (no devkit, no gems — the Brain is pure
# stdlib), extracts it, and keeps only what ruby.exe needs at runtime:
#   bin/ + lib/  (drops include/, packages/, ridk_use/, share/doc, share/ri and
#                 the static import lib — all C-extension / ri / rdoc tooling).

$ErrorActionPreference = 'Stop'

$RubyVersion = '3.2.8-1'
$AssetName   = "rubyinstaller-$RubyVersion-x64.7z"
$Tag         = "RubyInstaller-$RubyVersion"
$Url         = "https://github.com/oneclick/rubyinstaller2/releases/download/$Tag/$AssetName"

$RepoRoot = Split-Path -Parent $PSScriptRoot   # Tools/ -> repo root
$Dst      = Join-Path $RepoRoot 'ThirdParty/ruby'

$Tmp     = Join-Path ([System.IO.Path]::GetTempPath()) "rubyinstaller-$RubyVersion"
$Archive = "$Tmp.7z"

Write-Host "[1/4] Downloading $AssetName ..."
Invoke-WebRequest -Uri $Url -OutFile $Archive -UseBasicParsing

Write-Host "[2/4] Extracting ..."
$Extract = Join-Path $Tmp 'extract'
Remove-Item -Recurse -Force $Extract -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Path $Extract | Out-Null
tar -xf $Archive -C $Extract

$Inner = Join-Path $Extract "rubyinstaller-$RubyVersion-x64"

Write-Host "[3/4] Vendoring minimal runtime into ThirdParty/ruby ..."
Remove-Item -Recurse -Force $Dst -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Path $Dst | Out-Null
foreach ($d in 'bin','lib','share') {
    Copy-Item -Recurse (Join-Path $Inner $d) (Join-Path $Dst $d)
}
Copy-Item (Join-Path $Inner 'LICENSE.txt') (Join-Path $Dst 'LICENSE.txt')

# Runtime-unneeded: RDoc/ri docs, C-extension headers, static import lib.
Remove-Item -Recurse -Force (Join-Path $Dst 'share\doc'),(Join-Path $Dst 'share\ri') -ErrorAction SilentlyContinue
Remove-Item -Recurse -Force (Join-Path $Dst 'lib\pkgconfig') -ErrorAction SilentlyContinue
Get-ChildItem (Join-Path $Dst 'lib') -Filter 'libx64-ucrt-ruby*.dll.a' -ErrorAction SilentlyContinue |
    Remove-Item -Force -ErrorAction SilentlyContinue
if ((Get-ChildItem (Join-Path $Dst 'share') -ErrorAction SilentlyContinue | Measure-Object).Count -eq 0) {
    Remove-Item -Recurse -Force (Join-Path $Dst 'share')
}

Write-Host "[4/4] Verifying ..."
$RubyExe = Join-Path $Dst 'bin\ruby.exe'
if (-not (Test-Path $RubyExe)) { throw "Missing $RubyExe" }
& $RubyExe -v

Remove-Item -Force $Archive -ErrorAction SilentlyContinue
Remove-Item -Recurse -Force $Tmp -ErrorAction SilentlyContinue
Write-Host "Done. Vendored Ruby at $Dst"
