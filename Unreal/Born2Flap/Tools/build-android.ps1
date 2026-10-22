# Cook + stage + package Born2Flap for Android (Arm64 / Vulkan).
#
# Uses the launcher's engine install (V:\UE_5.8\UE_5.8) which carries the
# Android target platform; the top-level V:\UE_5.8 (the old registry target)
# does NOT have Engine/Build/Android and cannot build Android targets.
#
# Prerequisites:
#   1. Android SDK/NDK/JDK (see build-android notes): V:\SHARED\AndroidSDK,
#      NDK r27c (27.2.12479018), Android Studio JBR as JAVA_HOME.
#   2. The physics backend for Android: Tools/build-math-wasm.sh then
#      Tools/build-wasm3-android.sh must have produced Native/lib/libborn2flap_math_wasm.a
#      (the Android target links it statically; without it the build fails to link).
#
# Usage:
#   powershell -File Tools/build-android.ps1            # Development build
#   powershell -File Tools/build-android.ps1 -Shipping  # Shipping build
param(
    [string]$EngineRoot = 'V:\UE_5.8\UE_5.8',
    [switch]$Shipping
)

$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '../../..')).Path
$project = Join-Path $repo 'Unreal/Born2Flap/Born2Flap.uproject'
$config = if ($Shipping) { 'Shipping' } else { 'Development' }
$archive = Join-Path $repo 'build-current/Android'

# Android toolchain locations (matches the setup done for this machine).
$env:ANDROID_HOME = 'V:\SHARED\AndroidSDK'
$env:ANDROID_SDK_ROOT = 'V:\SHARED\AndroidSDK'
$env:NDKROOT = 'V:\SHARED\AndroidSDK\ndk\27.2.12479018'
$env:NDK_ROOT = $env:NDKROOT
$env:JAVA_HOME = 'G:\Programs\AndroidStudio\jbr'

$physicsLib = Join-Path $repo 'Native/lib/libborn2flap_math_wasm.a'
if (!(Test-Path -LiteralPath $physicsLib)) {
    Write-Host "WARNING: physics backend missing: $physicsLib" -ForegroundColor Yellow
    Write-Host "  Run Tools/build-math-wasm.sh then Tools/build-wasm3-android.sh first." -ForegroundColor Yellow
}

$uat = Join-Path $EngineRoot 'Engine/Build/BatchFiles/RunUAT.bat'
if (!(Test-Path -LiteralPath $uat)) {
    throw "RunUAT.bat not found at $uat"
}

Write-Host "== Android BuildCookRun ($config) ==" -ForegroundColor Cyan
Write-Host "Engine: $EngineRoot"
Write-Host "Project: $project"
Write-Host "Archive: $archive"

& $uat BuildCookRun `
    -project="$project" `
    -platform=Android `
    -clientconfig=$config `
    -cook -stage -package -build -archive `
    -archivedirectory="$archive" `
    -utf8output

Write-Host "BUILD_EXIT=$LASTEXITCODE"
exit $LASTEXITCODE
