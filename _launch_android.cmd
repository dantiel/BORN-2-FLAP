@echo off
rem Launcher for the UE Android build — uses %~dp0 to survive the space in the repo path.
cd /d "%~dp0"
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0Unreal\Born2Flap\Tools\build-android.ps1" > "%~dp0_android_build.out.log" 2> "%~dp0_android_build.err.log"
echo EXITCODE=%ERRORLEVEL% >> "%~dp0_android_build.out.log"
