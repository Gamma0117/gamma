@echo off
setlocal
cd /d "%~dp0"
powershell.exe -NoProfile -STA -ExecutionPolicy RemoteSigned -File "%~dp0AuroraRelay.ps1"
if errorlevel 1 (
  echo.
  echo Could not start Aurora Relay. Read README_KO.md for setup and script policy instructions.
  pause
)
endlocal
