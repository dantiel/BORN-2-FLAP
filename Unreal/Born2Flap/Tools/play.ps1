param([string]$EngineRoot = 'V:\UE_5.8', [ValidateSet('Menu','Ravenstonefield','Nature','Training','Shiomori')][string]$Level = 'Menu')
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
    'Shiomori'        { $map = '/Game/Shiomori/Maps/SHIOMORI'; $levelFlag = 'Shiomori' }
    'Ravenstonefield' { $map = '/Game/Ravenstonefield/Maps/RAVENSTONEFIELD'; $levelFlag = 'Ravenstonefield' }
    'Nature'          { $map = '/Game/Ravenstonefield/Maps/RAVENSTONEFIELD'; $levelFlag = 'Ravenstonefield' }
    'Training'        { $map = '/Engine/Maps/Entry'; $levelFlag = 'Training' }
}
# Fit the windowed window to the primary monitor's work area rather than a fixed
# 1600x900, which is larger than some displays (e.g. 1536x864 at 125% scaling)
# and would open the game window partly off-screen — pressing F11 re-fitted it.
Add-Type -AssemblyName System.Windows.Forms
$work = [System.Windows.Forms.Screen]::PrimaryScreen.WorkingArea
$resX = [Math]::Max(1024, $work.Width - 16)
$resY = [Math]::Max(576, $work.Height - 48)
$arguments = @(('\"'+$project+'\"'),$map,'-game','-windowed',('-ResX=' + $resX),('-ResY=' + $resY))
if ($levelFlag) { $arguments += ('-B2FLevel='+$levelFlag) }
$arguments += ('-abslog=\"'+$log+'\"')
# NOTE: Do NOT pass -WindowStyle Hidden here. That sets STARTF_USESHOWWINDOW
# with SW_HIDE on the process, which hides the GUI game window (the game then
# runs in the background with audio but no window). The -Cmd.exe test scripts
# hide the console window legitimately; this GUI launcher must stay visible.
Start-Process -FilePath (Join-Path $EngineRoot 'Engine/Binaries/Win64/UnrealEditor.exe') -ArgumentList $arguments -WorkingDirectory (Join-Path $EngineRoot 'Engine/Binaries/Win64') -PassThru