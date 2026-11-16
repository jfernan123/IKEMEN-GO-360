@echo off
rem Thanos as one of the two main agents (AlphaStar's league: one network per character, all training at once
rem against a shared league). Runs beside train_main_cap.bat; train_main_agents.bat starts both.
rem   256 of 512 fights are Thanos vs Thanos: player 2 is the current Thanos (35%) or one of his snapshots (PFSP).
rem   256 are Thanos vs Cap: player 2 is Cap's current network, re-read from rl\checkpoints\main_cap.pt every
rem   10 updates, or one of Cap's snapshots in rl\checkpoints\main_cap_league\ picked by PFSP.
rem   A snapshot joins every 300 updates (~10M decisions), 16 kept. SmartAI Captain America, Venom, Doom, Darkseid and
rem   Nightwing are only a check (64 matches each every 150 updates). 8 skills, ReDo, entropy 0.003; 4 threads a pool.
rem Starts from rl\checkpoints\league_thanos.pt (stop train_league_thanos first). Pass --resume to continue
rem rl\checkpoints\main_thanos.pt. Log: rl\checkpoints\main_thanos.log.
setlocal
cd /d "%~dp0"
set "OPP=chars/captainamerica/captainamerica.def,chars/mvci venom/mvci venom.def,chars/doom/doom.def"
set "OPP=%OPP%,chars/darkseid/darkseid.def,chars/nightwing/nightwing.def"
rl\.venv\Scripts\python.exe rl\train_pool.py --exe build\rl_new\mugen360.exe --game "DC vs MARVEL 2025" ^
  --p1 chars/thanos/thanos.def --p2 "%OPP%" --stage stages/gotham.def ^
  --fights 512 --league 256 --league-self 0.35 --snapshot-every 300 --league-size 16 ^
  --rival chars/captainamerica/captainamerica.def --rival-fights 256 --rival-checkpoint rl\checkpoints\main_cap.pt ^
  --rival-start rl\checkpoints\league_cap_final.pt --rival-refresh 10 ^
  --check-every 150 --check-matches 64 --threads 4 ^
  --rollout 64 --size large --steps 300000000 --entropy-coef 0.003 ^
  --skills 8 --diversity-coef 0.001 --diversity-min-win 0.6 --redo-every 20 ^
  --init-from rl\checkpoints\league_thanos.pt --checkpoint rl\checkpoints\main_thanos.pt --logdir rl\runs\main_thanos %* ^
  >> rl\checkpoints\main_thanos.log 2>&1
