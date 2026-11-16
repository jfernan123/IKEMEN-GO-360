@echo off
setlocal
cd /d "%~dp0"
rl\.venv\Scripts\python.exe rl\replay_win.py %*
