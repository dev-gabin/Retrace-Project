@echo off
setlocal
cd /d "%~dp0"

start "" powershell.exe -NoProfile -WindowStyle Hidden -Command "Start-Sleep -Seconds 1; Start-Process 'http://127.0.0.1:8765/'"
python "%~dp0debug_chat_web.py"

if errorlevel 1 (
    echo.
    echo Web debug tool stopped with an error. Check Python and port 8765.
    pause
)
