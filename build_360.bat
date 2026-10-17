@echo off
rem Builds the Xbox 360 version in WSL Debian: dist\mugen360\default.xex with its data\ folder.
cd /d "%~dp0"
wsl.exe -d Debian --cd "%CD%" --exec bash ./build_360.sh || exit /b 1
