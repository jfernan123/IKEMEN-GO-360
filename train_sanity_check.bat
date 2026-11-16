@echo off
rem Sanity check: one fixed matchup (Captain America mirror on Gotham) against SmartAI. Random inputs lose every
rem match here, so a win rate that climbs well above 0%% means the policy is really learning.
setlocal
cd /d "%~dp0"
rl\.venv\Scripts\python.exe rl\train_ppo.py --exe build\rl\mugen360.exe --game "DC vs MARVEL 2025" --p1 chars/captainamerica/captainamerica.def ^
  --p2 chars/captainamerica/captainamerica.def --stage stages/gotham.def --gamma 0.997 --steps 1000000 --no-monitor ^
  --checkpoint rl\checkpoints\sanity_captainamerica.pt --replay-dir rl\wins_sanity %*
