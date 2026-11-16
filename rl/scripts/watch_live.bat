@echo off
rem Watch the merged Captain America policy (rl\checkpoints\cap_merged.pt) fight live in the game window,
rem match after match, each match in a random one of its styles (skills), sampling its moves as in training.
rem Close the window to stop. Options: --p2 chars/doom/doom.def (pick the opponent), --skill N (one style),
rem --greedy (always its likeliest move), --matches N.
setlocal
cd /d "%~dp0..\.."
rl\.venv\Scripts\python.exe rl\watch_live.py --checkpoint rl\checkpoints\cap_merged.pt ^
  --p1 chars/captainamerica/captainamerica.def --p2 chars/thanos/thanos.def %*
