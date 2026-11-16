@echo off
rem Trains a Cap specialist for all 151 characters, three at a time, each to 95% or 200M; see rl\scratch_queue.py.
setlocal
cd /d "%~dp0"
rl\.venv\Scripts\python.exe rl\scratch_queue.py
pause
