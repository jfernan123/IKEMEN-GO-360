@echo off
rem Starts both main agents, Cap and Thanos, each in its own window (see train_main_cap.bat / train_main_thanos.bat).
rem Pass --resume to continue both. Logs: rl\checkpoints\main_cap.log and main_thanos.log.
cd /d "%~dp0"
start "Main agent: Captain America" cmd /c train_main_cap.bat %*
start "Main agent: Thanos" cmd /c train_main_thanos.bat %*
