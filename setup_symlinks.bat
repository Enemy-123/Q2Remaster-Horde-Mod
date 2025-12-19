@echo off
:: Run the PowerShell script with admin privileges if needed

:: Check for admin
net session >nul 2>&1
if %errorlevel% neq 0 (
    echo Requesting admin privileges...
    powershell -Command "Start-Process -FilePath '%~dp0setup_symlinks.ps1' -Verb RunAs -Wait"
) else (
    powershell -ExecutionPolicy Bypass -File "%~dp0setup_symlinks.ps1"
)
pause
