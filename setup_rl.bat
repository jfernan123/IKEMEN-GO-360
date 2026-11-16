@echo off
setlocal
cd /d "%~dp0"
C:\Python312\python.exe -m venv rl\.venv || exit /b 1
rl\.venv\Scripts\python.exe -m pip install --upgrade pip || exit /b 1
where nvidia-smi >nul 2>nul
if errorlevel 1 (
  echo No NVIDIA GPU found; installing CPU PyTorch.
  rl\.venv\Scripts\python.exe -m pip install --index-url https://download.pytorch.org/whl/cpu torch || exit /b 1
) else (
  echo NVIDIA GPU found; installing CUDA 12.6 PyTorch.
  rl\.venv\Scripts\python.exe -m pip install --upgrade --index-url https://download.pytorch.org/whl/cu126 torch || exit /b 1
)
rl\.venv\Scripts\python.exe -m pip install numpy tensorboard || exit /b 1
echo RL environment ready in rl\.venv
