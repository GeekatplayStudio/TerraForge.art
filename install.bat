@echo off
rem Geekatplay TerraForge - one-click install for Windows.
rem Double-click this file. It checks the tools TerraForge needs, offers to
rem install the missing ones, fetches the dependencies, builds, and puts
rem TerraForge in the Start Menu.
rem
rem Nothing here needs administrator rights: the application installs under
rem your own account (%LOCALAPPDATA%\Programs\TerraForge).
setlocal
cd /d "%~dp0"
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\install.ps1" %*
if errorlevel 1 (
    echo.
    echo Installation did not finish. The messages above say why.
    pause
    exit /b 1
)
pause
