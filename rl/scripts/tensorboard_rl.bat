@echo off
setlocal
cd /d "%~dp0..\.."
if not exist rl\.venv\Scripts\tensorboard.exe (
  echo TensorBoard is not installed. Run setup_rl.bat first.
  exit /b 1
)
echo TensorBoard: http://localhost:6006
start "" http://localhost:6006
rl\.venv\Scripts\tensorboard.exe --logdir rl\runs --port 6006
