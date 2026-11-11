param([string]$EngineRoot = 'V:\UE_5.8',
      [string]$Library = 'V:\BORN 2 FLAP\Unreal\Born2Flap\Binaries\ThirdParty\born2flap_math_rollfix.dll',
      [string]$LogName = 'onecase.log',
      [string]$Flag = 'B2FRollResponseTest',
      [string]$Marker = 'RollResponseTest',
      [int]$PhysicsHz = 960,
      [string]$Map = '/Engine/Maps/Entry')
$ErrorActionPreference = 'Stop'
$project = (Resolve-Path 'V:\BORN 2 FLAP\Unreal\Born2Flap\Born2Flap.uproject').Path
$logs = 'V:\BORN 2 FLAP\Unreal\Born2Flap\Saved\Logs'
New-Item -ItemType Directory -Path $logs -Force | Out-Null
$log = Join-Path $logs $LogName
$arguments = @(('"'+$project+'"'),$Map,'-game','-nullrhi','-nosound','-unattended','-nosplash','-benchmark','-fps=60',
  '-B2FFlightTest',('-'+$Flag),('-B2FPhysicsHz='+$PhysicsHz),
  ('-B2FPhysicsTestLibrary="'+(Resolve-Path -LiteralPath $Library).Path+'"'),
  ('-abslog="'+$log+'"'))
$process = Start-Process -FilePath (Join-Path $EngineRoot 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe') -ArgumentList $arguments -WindowStyle Hidden -PassThru
if (!$process.WaitForExit(420000)) {
    Stop-Process -Id $process.Id
    Write-Output ($LogName+' TIMEOUT')
} else {
    $result = Select-String -LiteralPath $log -Pattern $Marker -SimpleMatch
    if ($result) { $result.Line } else { Write-Output ($LogName+': no '+$Marker+' line') }
}
