param([string]$Map='/Engine/Maps/Entry')
$ErrorActionPreference='Stop'
$repo=Split-Path $PSScriptRoot
$running=Get-CimInstance Win32_Process -Filter "Name='UnrealEditor.exe'" |
    Where-Object {$_.CommandLine -match 'Born2Flap' -and $_.CommandLine -match ' -game'}
if($running){throw 'Close the existing BORN 2 FLAP game before starting a capture.'}
$stamp=Get-Date -Format 'yyyyMMdd-HHmmss'
$out=Join-Path $repo ".setup/tail-control-$stamp"
New-Item -ItemType Directory -Path $out -Force | Out-Null
$dll=Join-Path $repo 'Unreal/Born2Flap/Binaries/ThirdParty/born2flap_math.dll'
$preferences=Join-Path $repo 'Unreal/Born2Flap/Saved/Config/FlightPreferences.ini'
Copy-Item -LiteralPath $dll -Destination (Join-Path $out 'physics.dll')
Copy-Item -LiteralPath $preferences -Destination (Join-Path $out 'preferences.ini')
(Get-FileHash -LiteralPath $dll).Hash | Set-Content (Join-Path $out 'sha256.txt')
$log=Join-Path $out 'flight.log'
$project=Join-Path $repo 'Unreal/Born2Flap/Born2Flap.uproject'
# Interactive game: the pilot needs this window to reproduce the handling.
# No build/install, automated inputs, test fixture, or preference overrides.
$game=Start-Process 'V:/UE_5.8/Engine/Binaries/Win64/UnrealEditor.exe' -ArgumentList @(
  ('"'+$project+'"'),$Map,'-game','-B2FTailControlTrace',('-abslog="'+$log+'"')
) -WorkingDirectory 'V:/UE_5.8/Engine/Binaries/Win64' -WindowStyle Normal -PassThru
Write-Output "Game PID=$($game.Id). Capture: $log"
