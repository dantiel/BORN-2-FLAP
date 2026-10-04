param([string]$EngineRoot = 'V:\UE_5.8', [ValidateSet('Ravenstonefield','Nature','Training','Shiomori')][string]$Level = 'Ravenstonefield')
$ErrorActionPreference = 'Stop'
& (Join-Path $PSScriptRoot 'build.ps1') -EngineRoot $EngineRoot
$project = (Resolve-Path (Join-Path $PSScriptRoot '../Born2Flap.uproject')).Path
$log = Join-Path (Split-Path $project) ('Saved/Logs/play-'+$Level+'.log')
$map = if ($Level -eq 'Shiomori') { '/Game/Shiomori/Maps/SHIOMORI' } elseif ($Level -eq 'Training') { '/Engine/Maps/Entry' } else { '/Game/Ravenstonefield/Maps/RAVENSTONEFIELD' }
# NOTE: Do NOT pass -WindowStyle Hidden here. That sets STARTF_USESHOWWINDOW
# with SW_HIDE on the process, which hides the GUI game window (the game then
# runs in the background with audio but no window). The -Cmd.exe test scripts
# hide the console window legitimately; this GUI launcher must stay visible.
# The level selector (world browser) is the home screen now: do NOT pass -B2FLevel,
# which the GameMode treats as an explicit level request and skips the menu for.
# The selected map is still preloaded as the menu's backdrop; FLY then travels to
# the chosen world with ?Level=&SkipMenu=1 (no menu loop).
Start-Process -FilePath (Join-Path $EngineRoot 'Engine/Binaries/Win64/UnrealEditor.exe') -ArgumentList @(('\"'+$project+'\"'),$map,'-game','-windowed','-ResX=1600','-ResY=900',('-abslog=\"'+$log+'\"')) -WorkingDirectory (Join-Path $EngineRoot 'Engine/Binaries/Win64') -PassThru