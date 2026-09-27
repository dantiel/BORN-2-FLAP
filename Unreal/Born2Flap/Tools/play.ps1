param([string]$EngineRoot = 'V:\UE_5.8', [ValidateSet('Ravenstonefield','Nature','Training')][string]$Level = 'Ravenstonefield')
$ErrorActionPreference = 'Stop'
& (Join-Path $PSScriptRoot 'build.ps1') -EngineRoot $EngineRoot
$project = (Resolve-Path (Join-Path $PSScriptRoot '../Born2Flap.uproject')).Path
$log = Join-Path (Split-Path $project) ('Saved/Logs/play-'+$Level+'.log')
$map = if ($Level -eq 'Training') { '/Engine/Maps/Entry' } else { '/Game/Ravenstonefield/Maps/RAVENSTONEFIELD' }
Start-Process -FilePath (Join-Path $EngineRoot 'Engine/Binaries/Win64/UnrealEditor.exe') -ArgumentList @(('"'+$project+'"'),$map,'-game','-windowed','-ResX=1600','-ResY=900',('-B2FLevel='+$Level),('-abslog="'+$log+'"')) -WorkingDirectory (Join-Path $EngineRoot 'Engine/Binaries/Win64') -PassThru
