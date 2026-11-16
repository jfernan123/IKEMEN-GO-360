@echo off
setlocal
cd /d "%~dp0"
set "MARVEL_GAME=%~1"
if "%MARVEL_GAME%"=="" set "MARVEL_GAME=DC vs MARVEL 2025"
rl\.venv\Scripts\python.exe rl\train_ppo.py --game "%MARVEL_GAME%" --self-play-chance 0 --resume
