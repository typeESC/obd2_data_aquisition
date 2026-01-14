@echo off
REM OBD2 Manager - Installation Script for Windows
echo ====================================
echo   OBD2 Manager Installer
echo ====================================
echo.

REM Check Python version
python --version 2>NUL
if errorlevel 1 (
    echo ERROR: Python not found! Please install Python 3.9+
    pause
    exit /b 1
)

echo Installing dependencies...
pip install PyQt6 pyqtgraph pandas numpy scipy requests matplotlib

if errorlevel 1 (
    echo.
    echo ERROR: Failed to install some dependencies.
    echo Try running: pip install -r requirements.txt
    pause
    exit /b 1
)

echo.
echo ====================================
echo   Installation Complete!
echo ====================================
echo.
echo To run the application:
echo   python -m obd2_manager
echo.
echo Or install as package:
echo   pip install -e .
echo   obd2-manager
echo.
pause
