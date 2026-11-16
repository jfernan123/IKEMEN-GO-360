@echo off
rem Thanos vs Thanos only, rewarded by the result alone: +1 a win, -1 a loss (no damage terms, no
rem style bonus), discount 0.999 so the result reaches back through the match. Random sides: each match he plays
rem player 1 or player 2 at random, so his league opponents play from both sides at full strength.
rem   All 512 fights are the mirror, in 2 pools: player 2 is the current Thanos (35%) or a snapshot picked by PFSP; a
rem   snapshot every 300 updates (~10M decisions), 16 kept. SmartAI Captain America, Venom, Doom, Darkseid and Nightwing are
rem   only a check (64 matches each every 150 updates). ReDo, entropy 0.003, 4 threads a pool.
rem Starts from rl\checkpoints\main_thanos_final.pt (the main agent at 266M). Pass --resume to continue rl\checkpoints\thanos_mirror.pt.
rem Log: rl\checkpoints\thanos_mirror.log. TensorBoard: rl\runs\thanos_mirror\.
setlocal
cd /d "%~dp0"
set "OPP=chars/captainamerica/captainamerica.def,chars/mvci venom/mvci venom.def,chars/doom/doom.def,chars/darkseid/darkseid.def"
set "OPP=%OPP%,chars/nightwing/nightwing.def"
rl\.venv\Scripts\python.exe rl\train_pool.py --exe build\rl_new\mugen360.exe --game "DC vs MARVEL 2025" ^
  --p1 chars/thanos/thanos.def --p2 "%OPP%" --stage stages/gotham.def ^
  --fights 512 --league 512 --pools 2 --random-sides --league-self 0.35 --snapshot-every 300 --league-size 16 ^
  --check-every 150 --check-matches 64 --threads 4 ^
  --rollout 64 --size large --steps 300000000 --entropy-coef 0.003 ^
  --damage-dealt-weight 0 --damage-taken-weight 0 --gamma 0.999 ^
  --skills 8 --diversity-coef 0 --redo-every 20 ^
  --init-from rl\checkpoints\main_thanos_final.pt --checkpoint rl\checkpoints\thanos_mirror.pt ^
  --logdir rl\runs\thanos_mirror %* >> rl\checkpoints\thanos_mirror.log 2>&1
