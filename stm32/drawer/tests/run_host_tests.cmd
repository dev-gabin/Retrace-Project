@echo off
setlocal
rem Use an existing VS developer environment, or the verified local installation.
where cl >nul 2>nul
if errorlevel 1 call "C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b 1
pushd "%~dp0.."
if not exist build\host mkdir build\host
cd build\host
cl /nologo /W4 /WX /utf-8 /D_CRT_SECURE_NO_WARNINGS /std:c11 /I../../tests/stubs /I../../Core/Inc ../../tests/test_firmware.c ../../Core/Src/cmd_parser.c ../../Core/Src/serial_cmd.c ../../Core/Src/drawer.c ../../Core/Src/emergency_button.c /Fe:test_firmware.exe
if errorlevel 1 goto fail
test_firmware.exe
if errorlevel 1 goto fail
popd
pushd "%~dp0..\..\laser_head"
if not exist build\host mkdir build\host
cd build\host
cl /nologo /W4 /WX /utf-8 /D_CRT_SECURE_NO_WARNINGS /std:c11 /I../../Core/Inc ../../tests/test_cmd_parser.c ../../Core/Src/cmd_parser.c /Fe:test_cmd_parser.exe
if errorlevel 1 goto fail
test_cmd_parser.exe
if errorlevel 1 goto fail
popd
exit /b 0
:fail
popd
exit /b 1
