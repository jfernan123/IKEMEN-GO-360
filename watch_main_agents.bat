@echo off
rem Shows the two main agents' logs live, one window each (the training windows write to the logs, not the screen).
cd /d "%~dp0"
start "Cap log" powershell -NoProfile -Command "Get-Content rl\checkpoints\main_cap.log -Tail 20 -Wait"
start "Thanos log" powershell -NoProfile -Command "Get-Content rl\checkpoints\main_thanos.log -Tail 20 -Wait"
