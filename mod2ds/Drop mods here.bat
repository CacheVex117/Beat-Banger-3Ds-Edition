@echo off
rem Drag Beat Banger mod folders or bbmods .zip files onto this file (or onto a shortcut to it).
rem They are converted for the 3DS and copied to the 3DS SD card (sdmc:/3ds/bb_mods) or to Azahar.
chcp 65001 >nul
cd /d "%~dp0"
title MOD2DS - Beat Banger mods to 3DS
if "%~1"=="" (
  echo.
  echo   Drag Beat Banger mod folders or .zip files onto "Drop mods here.bat".
  echo   You can also drop your whole mods folder:
  echo     %APPDATA%\Godot\app_userdata\Beat Banger\mods
  echo.
  pause
  exit /b 1
)
python -m mod2ds install %*
echo.
pause
