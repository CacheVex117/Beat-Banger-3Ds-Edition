@echo off
rem Host (MSVC) tests for the portable core and the audio mock. Run from anywhere.
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d "%~dp0\.."
if not exist build\host mkdir build\host
if not exist build\host\audio mkdir build\host\audio
if not exist build\host\save mkdir build\host\save
cl /nologo /W4 /WX /D_CRT_SECURE_NO_WARNINGS /O2 /I core core\rhythm_core.c core\chart_loader.c platform\host\test_rhythm_core.c /Fe:build\host\test_rhythm_core.exe /Fo:build\host\ || exit /b 1
build\host\test_rhythm_core.exe || exit /b 1
cl /nologo /W4 /WX /D_CRT_SECURE_NO_WARNINGS /O2 /I core /I platform\3ds platform\3ds\audio_ndsp.c platform\host\test_audio_mock.c /Fe:build\host\test_audio_mock.exe /Fo:build\host\audio\ || exit /b 1
build\host\test_audio_mock.exe || exit /b 1
cl /nologo /W4 /WX /D_CRT_SECURE_NO_WARNINGS /O2 /I core core\save_data.c platform\host\test_save_input.c /Fe:build\host\test_save_input.exe /Fo:build\host\save\ || exit /b 1
build\host\test_save_input.exe || exit /b 1
cl /nologo /W4 /WX /D_CRT_SECURE_NO_WARNINGS /O2 /I core core\save_data.c core\progress.c platform\host\test_progress.c /Fe:build\host\test_progress.exe /Fo:build\host\save\ || exit /b 1
build\host\test_progress.exe || exit /b 1
