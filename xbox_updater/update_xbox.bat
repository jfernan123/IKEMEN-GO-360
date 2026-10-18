@echo off
rem Double-click: puts the newest mugen360 build on the Xbox and shares the last session's log. See README.txt.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0update_xbox.ps1"
pause
