@echo off
setlocal
where cl >nul 2>nul
if errorlevel 1 (
    if not exist "C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars64.bat" (
        echo MSVC not found. Run this script from a Visual Studio Developer Command Prompt.
        exit /b 1
    )
    call "C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars64.bat" >nul
    if errorlevel 1 exit /b 1
)
pushd "%~dp0.."
if not exist "build\host" mkdir "build\host"
cl /nologo /W4 /WX /EHsc /std:c++14 /utf-8 /Iinclude tests\test_entrance_control.cpp /Febuild\host\test_entrance_control.exe /Fobuild\host\test_entrance_control.obj
if errorlevel 1 (
    popd
    exit /b 1
)
build\host\test_entrance_control.exe
set "test_result=%errorlevel%"
popd
exit /b %test_result%
