@echo off
setlocal
set "JAVA_HOME=G:\Programs\jdk-17\jdk-17.0.21+9"
set "PATH=%JAVA_HOME%\bin;%PATH%"
cd /d "V:\BORN 2 FLAP\Unreal\Born2Flap\Intermediate\Android\arm64\gradle"
echo JAVA_HOME=%JAVA_HOME%
call rungradle.bat :app:assembleDebug
echo GRADLE_EXIT=%ERRORLEVEL%
exit /b %ERRORLEVEL%
