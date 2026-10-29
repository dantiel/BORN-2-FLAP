param([string]$EngineRoot = 'V:\UE_5.8', [ValidateSet('Menu','Ravenstonefield','Nature','Training','Shiomori','Yunsha','NeonAbyss')][string]$Level = 'Menu')
$ErrorActionPreference = 'Stop'
& (Join-Path $PSScriptRoot 'build.ps1') -EngineRoot $EngineRoot
$project = (Resolve-Path (Join-Path $PSScriptRoot '../Born2Flap.uproject')).Path
$log = Join-Path (Split-Path $project) ('Saved/Logs/play-'+$Level+'.log')
# The menu (home screen) boots the minimal Entry map with no level flag, so no
# world is preloaded — a level only loads when the player selects one and clicks
# FLY. An explicit -Level boots straight into that world (in-game, no menu).
$map = '/Engine/Maps/Entry'
$levelFlag = $null
switch ($Level) {
    'Yunsha' { $map = '/Game/Yunsha/Maps/YUNSHA'; $levelFlag = 'Yunsha' }
    'NeonAbyss' { $map = '/Game/NeonAbyss/Maps/NEONABYSS'; $levelFlag = 'NeonAbyss' }
    'Shiomori'        { $map = '/Game/Shiomori/Maps/SHIOMORI'; $levelFlag = 'Shiomori' }
    'Ravenstonefield' { $map = '/Game/Ravenstonefield/Maps/RAVENSTONEFIELD'; $levelFlag = 'Ravenstonefield' }
    'Nature'          { $map = '/Game/Ravenstonefield/Maps/RAVENSTONEFIELD'; $levelFlag = 'Ravenstonefield' }
    'Training'        { $map = '/Engine/Maps/Entry'; $levelFlag = 'Training' }
}
# Launch borderless fullscreen at the display's native resolution. The engine
# reads FullscreenMode=1 (WindowedFullscreen) from DefaultGameUserSettings.ini,
# which fills the primary monitor without a mode switch — UE's native default.
# Do NOT pass -windowed/-ResX/-ResY here: that forces a windowed game (and a
# smaller-than-native window + loading screen).
$arguments = @(('\"'+$project+'\"'),$map,'-game')
if ($levelFlag) { $arguments += ('-B2FLevel='+$levelFlag) }
$arguments += ('-abslog=\"'+$log+'\"')
# NOTE: Do NOT pass -WindowStyle Hidden here. That sets STARTF_USESHOWWINDOW
# with SW_HIDE on the process, which hides the GUI game window (the game then
# runs in the background with audio but no window). The -Cmd.exe test scripts
# hide the console window legitimately; this GUI launcher must stay visible.
Start-Process -FilePath (Join-Path $EngineRoot 'Engine/Binaries/Win64/UnrealEditor.exe') -ArgumentList $arguments -WorkingDirectory (Join-Path $EngineRoot 'Engine/Binaries/Win64') -PassThru