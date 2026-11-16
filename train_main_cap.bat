@echo off
rem Captain America as one of the two main agents (AlphaStar's league: one network per character, all training at once
rem against a shared league). Runs beside train_main_thanos.bat; train_main_agents.bat starts both.
rem   256 of 512 fights are Cap vs Cap: player 2 is the current Cap (35%) or one of his snapshots picked by PFSP.
rem   256 are Cap vs Thanos: player 2 is Thanos's current network, re-read from rl\checkpoints\main_thanos.pt every
rem   10 updates, or one of Thanos's snapshots in rl\checkpoints\main_thanos_league\ picked by PFSP.
rem   A snapshot joins every 300 updates (~10M decisions), 16 kept. SmartAI Thanos, Venom, Doom, Darkseid and Nightwing
rem   are only a check (64 matches each every 150 updates). 8 skills, ReDo, entropy 0.003; 4 threads a pool, so the
rem   two runs share the 16 cores.
rem Reward: win/loss only (+1 / -1, AlphaStar's), no damage terms; discount 0.999 so the result reaches back
rem through the match (the turtling Cap of the first run, damage dealt - taken, is main_cap_turtle.pt).
rem Starts from rl\checkpoints\league_cap_final.pt. Pass --resume to continue rl\checkpoints\main_cap.pt.
rem Log: rl\checkpoints\main_cap.log.
setlocal
cd /d "%~dp0"
set "OPP=chars/thanos/thanos.def,chars/mvci venom/mvci venom.def,chars/doom/doom.def,chars/darkseid/darkseid.def"
set "OPP=%OPP%,chars/nightwing/nightwing.def"
rl\.venv\Scripts\python.exe rl\train_pool.py --exe build\rl_new\mugen360.exe --game "DC vs MARVEL 2025" ^
  --p1 chars/captainamerica/captainamerica.def --p2 "%OPP%" --stage stages/gotham.def ^
  --fights 512 --league 256 --league-self 0.35 --snapshot-every 300 --league-size 16 ^
  --rival chars/thanos/thanos.def --rival-fights 256 --rival-checkpoint rl\checkpoints\main_thanos.pt ^
  --rival-start rl\checkpoints\league_thanos.pt --rival-refresh 10 ^
  --check-every 150 --check-matches 64 --threads 4 ^
  --rollout 64 --size large --steps 300000000 --entropy-coef 0.003 ^
  --damage-dealt-weight 0 --damage-taken-weight 0 --gamma 0.999 ^
  --skills 8 --diversity-coef 0.001 --diversity-min-win 0.6 --redo-every 20 ^
  --init-from rl\checkpoints\league_cap_final.pt --checkpoint rl\checkpoints\main_cap.pt --logdir rl\runs\main_cap %* ^
  >> rl\checkpoints\main_cap.log 2>&1
