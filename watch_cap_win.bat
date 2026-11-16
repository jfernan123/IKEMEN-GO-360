@echo off
rem Plays the newest Cap vs Thanos policy (rl\checkpoints\cap_vs_thanos_v2.pt) with graphics until Cap wins, then
rem opens the video (rl\clips\latest_cap_win.mp4). Needs build_pc_test.bat built with --rl-record.
setlocal
cd /d "%~dp0"
rl\.venv\Scripts\python.exe rl\watch_win.py --checkpoint rl\checkpoints\cap_vs_thanos_v2.pt ^
  --p1 chars/captainamerica/captainamerica.def --p2 chars/thanos/thanos.def --out rl\clips\latest_cap_win.mp4 %*
pause
