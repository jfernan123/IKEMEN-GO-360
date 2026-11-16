@echo off
rem Thanos's self-play league, starting from Cap's finished league policy (rl\checkpoints\league_cap_final.pt, 297M):
rem the network already knows the game, Thanos's own character input is new.
rem   All 512 fights are Thanos vs Thanos, in 2 pools (the current policy 35%, or a frozen snapshot picked by PFSP).
rem   SmartAI Captain America, Venom, Doom, Darkseid and Nightwing are only a check: 64 matches each every 150 updates
rem   (about 5M decisions), shown as "check" lines in the log and check/ in TensorBoard.
rem   8 skills, ReDo, entropy 0.003; no anchor (Cap's anchor is a Cap policy).
rem Snapshots go to rl\checkpoints\league_thanos_league\. Pass --resume to continue rl\checkpoints\league_thanos.pt.
setlocal
cd /d "%~dp0"
set "OPP=chars/captainamerica/captainamerica.def,chars/mvci venom/mvci venom.def,chars/doom/doom.def"
set "OPP=%OPP%,chars/darkseid/darkseid.def,chars/nightwing/nightwing.def"
rl\.venv\Scripts\python.exe rl\train_pool.py --exe build\rl_new\mugen360.exe --game "DC vs MARVEL 2025" ^
  --p1 chars/thanos/thanos.def --p2 "%OPP%" --stage stages/gotham.def ^
  --fights 512 --league 512 --pools 2 --league-self 0.35 --snapshot-every 300 --league-size 16 ^
  --check-every 150 --check-matches 64 ^
  --rollout 64 --size large --steps 300000000 --entropy-coef 0.003 ^
  --skills 8 --diversity-coef 0.001 --diversity-min-win 0.6 --redo-every 20 ^
  --init-from rl\checkpoints\league_cap_final.pt --checkpoint rl\checkpoints\league_thanos.pt %*