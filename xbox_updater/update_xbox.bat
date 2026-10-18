@echo off
rem Double-click and leave open: installs each new mugen360 build on the Xbox as soon as it's on GitHub. See README.txt.
title mugen360 Xbox updater
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0update_xbox.ps1"
pause
