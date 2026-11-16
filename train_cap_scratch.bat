@echo off
rem Captain America from scratch (a fresh network, no earlier policy), rewarded by the result alone: +1 a win, -1 a
rem loss, no damage terms. Time over is a loss for both (only a KO wins) and a drawn round ends the match at 0. A win
rem loses 0.0004 per decision it took (a 1,000-decision win is worth 0.6), so quicker wins are worth more; a loss is
rem -1 however quick. Discount 0.9995. Random sides, player 2's view mirrored by the engine. No style codes.
rem   384 of the 512 fights are the mirror: player 2 is the current Cap (35%) or a snapshot picked by PFSP; a snapshot
rem   every 300 updates (~10M decisions), 16 kept. The other 128 are SmartAI Thanos, Venom, Doom, Darkseid and
rem   Nightwing (also checked, 64 matches each every 150 updates). ReDo, entropy 0.003.
rem Pass --resume to continue rl\checkpoints\cap_scratch.pt. Log: rl\checkpoints\cap_scratch.log. TensorBoard:
rem rl\runs\cap_scratch\.
setlocal
cd /d "%~dp0"
set "OPP=chars/thanos/thanos.def,chars/mvci venom/mvci venom.def,chars/doom/doom.def,chars/darkseid/darkseid.def"
set "OPP=%OPP%,chars/nightwing/nightwing.def"
rl\.venv\Scripts\python.exe rl\train_pool.py --exe build\rl_new\mugen360.exe --game "DC vs MARVEL 2025" ^
  --p1 chars/captainamerica/captainamerica.def --p2 "%OPP%" --stage stages/gotham.def ^
  --fights 512 --league 384 --pools 2 --random-sides --league-self 0.35 --snapshot-every 300 --league-size 16 ^
  --check-every 150 --check-matches 64 ^
  --rollout 64 --size large --steps 600000000 --entropy-coef 0.003 --lr-max 1e-3 ^
  --damage-dealt-weight 0 --damage-taken-weight 0 --gamma 0.9995 --win-time-cost 0.0004 --time-over loss ^
  --redo-every 20 --checkpoint rl\checkpoints\cap_scratch.pt ^
  --logdir rl\runs\cap_scratch %* >> rl\checkpoints\cap_scratch.log 2>&1
