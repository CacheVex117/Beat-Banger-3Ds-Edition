@echo off
rem verify_charts.bat <native3ds dir> <mod2ds out dir>
rem Compiles host\verify_charts.c against the runtime's core\chart_loader.c (MSVC) and loads every
rem stages\*\charts\*.bbchart under the output folder. Nothing is written outside build\.
setlocal enabledelayedexpansion
if "%~2"=="" (echo usage: verify_charts.bat ^<native3ds dir^> ^<mod2ds out dir^> & exit /b 2)
set NATIVE=%~f1
set OUT=%~f2
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 2
cd /d "%~dp0\.."
if not exist build\host mkdir build\host
cl /nologo /W4 /WX /D_CRT_SECURE_NO_WARNINGS /O2 /I "%NATIVE%\core" "%NATIVE%\core\chart_loader.c" "%NATIVE%\core\rhythm_core.c" host\verify_charts.c /Fe:build\host\verify_charts.exe /Fo:build\host\ || exit /b 2
set FILES=
for /r "%OUT%\stages" %%f in (*.bbchart) do set FILES=!FILES! "%%f"
if "!FILES!"=="" (echo no .bbchart files under %OUT%\stages & exit /b 2)
build\host\verify_charts.exe !FILES!
