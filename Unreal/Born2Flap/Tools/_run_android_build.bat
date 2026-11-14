@echo off
cd /d "V:\BORN 2 FLAP\Unreal\Born2Flap"
powershell -NoProfile -ExecutionPolicy Bypass -File "Tools\build-android.ps1" >> "V:\BORN 2 FLAP\_android_build.log" 2>&1
echo BUILD_EXIT=%ERRORLEVEL% >> "V:\BORN 2 FLAP\_android_build.log"
