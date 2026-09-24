param([string]$EngineRoot = 'V:\UE_5.8', [ValidateSet('Nature','Training')][string]$Level = 'Nature')
$ErrorActionPreference = 'Stop'
$project = (Resolve-Path (Join-Path $PSScriptRoot '../Born2Flap.uproject')).Path
$log = Join-Path (Split-Path $project) 'Saved/Logs/play-training.log'
Start-Process -FilePath (Join-Path $EngineRoot 'Engine/Binaries/Win64/UnrealEditor.exe') -ArgumentList @(('"'+$project+'"'),'-game','-windowed','-ResX=1280','-ResY=800',('-B2FLevel='+$Level),('-abslog="'+$log+'"')) -WorkingDirectory (Join-Path $EngineRoot 'Engine/Binaries/Win64') -PassThru
