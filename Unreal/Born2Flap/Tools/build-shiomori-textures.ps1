param(
 [Parameter(Mandatory=$true)][string]$SandSource,
 [Parameter(Mandatory=$true)][string]$WhiteSource,
 [string]$OutputRoot=(Join-Path $PSScriptRoot '../Saved/ShiomoriSource'),
 [string]$PythonExe='V:\UE_5.8\Engine\Binaries\ThirdParty\Python3\Win64\python.exe'
)
$ErrorActionPreference='Stop'
Add-Type -AssemblyName System.Drawing
Add-Type -Path (Join-Path $PSScriptRoot 'ShiomoriTextureBuilder.cs') -ReferencedAssemblies System.Drawing
$root=[IO.Path]::GetFullPath($OutputRoot)
# Preserve user assets; choose a fresh OutputRoot when creating another version.
foreach($dir in @('ocean_waves','sand_beach','white_panel')){
 if(Test-Path (Join-Path $root ($dir+'/nor.jpg'))){throw 'Output already exists; use a new -OutputRoot.'}
}
New-Item -ItemType Directory -Force (Join-Path $root 'generated-originals') | Out-Null
Copy-Item -LiteralPath $SandSource -Destination (Join-Path $root 'generated-originals/sand-albedo.png')
Copy-Item -LiteralPath $WhiteSource -Destination (Join-Path $root 'generated-originals/white-albedo.png')
[ShiomoriTextureBuilder]::Technical($root)
& $PythonExe (Join-Path $PSScriptRoot 'encode-shiomori-textures.py') $root
if($LASTEXITCODE){throw 'JPG 4:4:4 encoding failed. Install Pillow into .setup/texture-deps.'}
$report=[ShiomoriTextureBuilder]::Validate($root)
$report | Set-Content (Join-Path $root 'generated-texture-validation.txt')
$report
