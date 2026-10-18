@echo off
rem Builds the Xbox 360 version in WSL Debian: dist\mugen360\default.xex with its data\ folder.
setlocal
cd /d "%~dp0"
rem Hands MUGEN360_VERSION (set by publish_360.bat) through to the build inside WSL.
if defined WSLENV (set "WSLENV=%WSLENV%:MUGEN360_VERSION") else set "WSLENV=MUGEN360_VERSION"
wsl.exe -d Debian --cd "%CD%" --exec bash ./build_360.sh || exit /b 1
