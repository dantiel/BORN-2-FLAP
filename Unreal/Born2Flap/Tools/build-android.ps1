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
$env:ANDROID_NDK_ROOT = $env:NDKROOT
$env:JAVA_HOME = 'G:\Programs\jdk-17\jdk-17.0.21+9'

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
    -clientconfig="$config" `
    -cook -stage -package -build -archive -pak `
    -archivedirectory="$archive" `
    -utf8output

# --- DLC chunk staging -----------------------------------------------------
# With bGenerateChunks=True, the cook emits pakchunk<N>-<platform>.pak files:
#   chunk 0 = base (Shiomori + shared content) -> stays inside the APK
#   chunk 1..4 = Murex / NeonAbyss / Ravenstonefield / Yunsha -> hosted as DLC
# Copy each non-base chunk to build-current/AndroidDLC/<World>.b2f-world so it can
# be served over HTTP and downloaded+mounted at runtime. Each pak carries the
# world's whole /Game/<World> directory, including its /Audio radio soundtrack.
# Chunk IDs MUST match Tools/create_dlc_chunks.py and Born2FlapDLCSubsystem.cpp.
$dlcChunks = @(
    @{ World = 'Murex';           Chunk = 1 },
    @{ World = 'NeonAbyss';       Chunk = 2 },
    @{ World = 'Ravenstonefield'; Chunk = 3 },
    @{ World = 'Yunsha';          Chunk = 4 }
)

if ($LASTEXITCODE -eq 0) {
    $dlcOut = Join-Path $repo 'build-current/AndroidDLC'
    New-Item -ItemType Directory -Force -Path $dlcOut | Out-Null
    foreach ($c in $dlcChunks) {
        $pattern = "pakchunk$($c.Chunk)-*.pak"
        $found = Get-ChildItem -Path $archive -Recurse -File -Filter $pattern -ErrorAction SilentlyContinue |
                 Where-Object { $_.Name -notmatch 'optional' } |
                 Sort-Object Length -Descending |
                 Select-Object -First 1
        if ($found) {
                        $dst = Join-Path $dlcOut "$($c.World).b2f-world"
                        Copy-Item -LiteralPath $found.FullName -Destination $dst -Force
                        Write-Host "DLC staged: $($c.World).b2f-world -> $dst ($([math]::Round($found.Length/1MB,1)) MB)" -ForegroundColor Green
        } else {
            Write-Host "WARNING: no staged pak found for chunk $($c.Chunk) ($($c.World))" -ForegroundColor Yellow
        }
    }

    # --- Refresh DLC manifest (size + sha1) -------------------------------
    $manifestPath = Join-Path $repo 'Unreal/Born2Flap/Config/DLCManifest.json'
    if (Test-Path -LiteralPath $manifestPath) {
        $manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
        foreach ($w in $manifest.worlds) {
            $pak = Join-Path $dlcOut "$($w.name).b2f-world"
            if (Test-Path -LiteralPath $pak) {
                $hash = Get-FileHash -Algorithm SHA1 -LiteralPath $pak
                $w.size_bytes = [int64](Get-Item -LiteralPath $pak).Length
                $w.sha1 = $hash.Hash.ToLowerInvariant()
                Write-Host "Manifest updated: $($w.name) sha1=$($w.sha1) size=$($w.size_bytes)" -ForegroundColor Green
            } else {
                Write-Host "Manifest: no staged pak for $($w.name); keeping existing size/sha1" -ForegroundColor Yellow
            }
        }
        $json = $manifest | ConvertTo-Json -Depth 10
        [System.IO.File]::WriteAllText($manifestPath, $json, (New-Object System.Text.UTF8Encoding($false)))
    } else {
        Write-Host "WARNING: DLC manifest not found: $manifestPath" -ForegroundColor Yellow
    }
} else {
    Write-Host "Cook failed; skipping DLC chunk staging." -ForegroundColor Yellow
}

Write-Host "BUILD_EXIT=$LASTEXITCODE"
exit $LASTEXITCODE