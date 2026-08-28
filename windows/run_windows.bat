@echo off
REM Run directly with an installed Python (no packaging step).
REM Requires Python 3 + tkinter, and libsgzzlb.dll (included in this kit).
REM Note: batch files are ASCII-only to avoid codepage issues.
setlocal
cd /d "%~dp0\.."

where python >nul 2>nul
if errorlevel 1 (
    echo [ERROR] python not found.
    echo Either install Python 3.8+ (with tkinter), or use
    echo build_windows.bat to produce a standalone exe instead.
    pause
    exit /b 1
)

python app\main.py
pause
endlocal
