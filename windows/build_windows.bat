@echo off
REM ============================================================
REM  sgzzlb (Three Kingdoms squad builder) - Windows one-click
REM  package: produces a standalone dist\sgzzlb.exe via PyInstaller
REM
REM  Note: batch files are ASCII-only to avoid codepage issues.
REM ============================================================
setlocal
cd /d "%~dp0"

where python >nul 2>nul
if errorlevel 1 (
    echo [ERROR] python not found.
    echo Install Python 3.8+ from https://www.python.org/downloads/
    echo and check "Add Python to PATH" during install.
    pause
    exit /b 1
)

if not exist "libsgzzlb.dll" (
    echo [ERROR] libsgzzlb.dll not found in this folder.
    echo Make sure the whole package is extracted before building.
    pause
    exit /b 1
)

echo [1/2] Installing/updating PyInstaller ...
python -m pip install --upgrade pyinstaller
if errorlevel 1 (
    echo [ERROR] Failed to install PyInstaller. Check your network.
    pause
    exit /b 1
)

echo [2/2] Building standalone exe (1-3 min) ...
python -m PyInstaller --clean --noconfirm sgzzlb_windows.spec
if errorlevel 1 (
    echo [ERROR] Build failed. Please share the error message above.
    pause
    exit /b 1
)

echo.
echo Done! exe is at: %~dp0dist\sgzzlb.exe
echo Double-click it to run. No Python needed on the target PC.
pause
endlocal
