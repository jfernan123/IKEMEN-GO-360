@echo off
setlocal
cd /d "%~dp0"

set "MARVEL_GAME=%~1"
if "%MARVEL_GAME%"=="" set "MARVEL_GAME=DC vs MARVEL 2025"

echo Starting a new randomly initialized policy.
echo Existing rl\checkpoints\marvel_policy.pt is preserved.
echo Scratch checkpoint: rl\checkpoints\marvel_policy_scratch.pt
echo.
rl\.venv\Scripts\python.exe rl\train_ppo.py --game "%MARVEL_GAME%" --checkpoint rl\checkpoints\marvel_policy_scratch.pt --replay-dir rl\wins_scratch --self-play-chance 0
