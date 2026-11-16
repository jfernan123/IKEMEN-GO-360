@echo off
setlocal
cd /d "%~dp0"

set "MATCHES=%~1"
if "%MATCHES%"=="" set "MATCHES=100"
set "MARVEL_GAME=%~2"
if "%MARVEL_GAME%"=="" set "MARVEL_GAME=DC vs MARVEL 2025"
set "ENVS=%~3"
if "%ENVS%"=="" set "ENVS=8"
set "TARGET=%~4"
if "%TARGET%"=="" set "TARGET=0.85"

echo Comparing the deterministic policy with random controls over %MATCHES% identical matchups on %ENVS% environments...
echo SmartAI qualification target: %TARGET%
rl\.venv\Scripts\python.exe rl\evaluate.py --game "%MARVEL_GAME%" --matches %MATCHES% --envs %ENVS% --target %TARGET% --agent both
pause
