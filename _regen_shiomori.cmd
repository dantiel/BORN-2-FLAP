@echo off
rem Launcher for the Shiomori map regeneration — uses %~dp0 to survive the space in the repo path.
cd /d "%~dp0"
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0.setup\regen-shiomori.ps1"
