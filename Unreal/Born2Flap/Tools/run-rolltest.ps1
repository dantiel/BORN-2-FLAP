param([string]$EngineRoot = 'V:\UE_5.8',
      [string]$Library = 'V:\BORN 2 FLAP\Unreal\Born2Flap\Binaries\ThirdParty\born2flap_math_rollfix.dll',
      [string]$LogName = 'rollfix.log')
$ErrorActionPreference = 'Stop'
$project = (Resolve-Path 'V:\BORN 2 FLAP\Unreal\Born2Flap\Born2Flap.uproject').Path
$logs = 'V:\BORN 2 FLAP\Unreal\Born2Flap\Saved\Logs'
New-Item -ItemType Directory -Path $logs -Force | Out-Null
$log = Join-Path $logs $LogName
$arguments = @(('"'+$project+'"'),'/Engine/Maps/Entry','-game','-nullrhi','-nosound','-unattended','-nosplash','-benchmark','-fps=60',
  '-B2FFlightTest','-B2FRollResponseTest','-B2FPhysicsHz=960',
  ('-B2FPhysicsTestLibrary="'+(Resolve-Path -LiteralPath $Library).Path+'"'),
  ('-abslog="'+$log+'"'))
$process = Start-Process -FilePath (Join-Path $EngineRoot 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe') -ArgumentList $arguments -WindowStyle Hidden -PassThru
if (!$process.WaitForExit(420000)) {
    Stop-Process -Id $process.Id
    Write-Output 'ROLLFIX TIMEOUT'
} else {
    $result = Select-String -LiteralPath $log -Pattern 'RollResponseTest' -SimpleMatch
    if ($result) { $result.Line } else { Write-Output 'ROLLFIX: no RollResponseTest line' }
}
