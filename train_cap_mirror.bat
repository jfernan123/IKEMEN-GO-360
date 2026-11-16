@echo off
rem Captain America rewarded by the result alone: +1 a win, -1 a loss (no damage terms, no style bonus), discount
rem 0.9995 so the result reaches back through the match. Random sides: each match he plays player 1 or player 2 at
rem random (player 2's view is mirrored by the engine, so both sides look alike to him).
rem   384 of the 512 fights are the mirror: player 2 is the current Cap (35%) or a snapshot picked by PFSP; a snapshot
rem   every 300 updates (~10M decisions), 16 kept. The other 128 are SmartAI Thanos, Venom, Doom, Darkseid and
rem   Nightwing (also checked, 64 matches each every 150 updates). ReDo, entropy 0.003.
rem From 300M on (2026-10-29) every decision costs 0.0005 (about 0.5 a typical match, 1 by time over): quicker wins
rem are worth more. From 414M on (2026-10-30): time over is a loss for both (only a KO wins), a drawn round ends the
rem match, SmartAI fights are back in, the learning rate tops out at 1e-3.
rem Starts from rl\checkpoints\league_cap_final.pt. Pass --resume to continue rl\checkpoints\cap_mirror.pt.
rem Log: rl\checkpoints\cap_mirror.log. TensorBoard: rl\runs\cap_mirror\.
setlocal
cd /d "%~dp0"
set "OPP=chars/thanos/thanos.def,chars/mvci venom/mvci venom.def,chars/doom/doom.def,chars/darkseid/darkseid.def"
set "OPP=%OPP%,chars/nightwing/nightwing.def"
rl\.venv\Scripts\python.exe rl\train_pool.py --exe build\rl_new\mugen360.exe --game "DC vs MARVEL 2025" ^
  --p1 chars/captainamerica/captainamerica.def --p2 "%OPP%" --stage stages/gotham.def ^
  --fights 512 --league 384 --pools 2 --random-sides --league-self 0.35 --snapshot-every 300 --league-size 16 ^
  --check-every 150 --check-matches 64 ^
  --rollout 64 --size large --steps 600000000 --entropy-coef 0.003 --lr-max 1e-3 ^
  --damage-dealt-weight 0 --damage-taken-weight 0 --gamma 0.9995 --step-penalty 0.0005 --time-over loss ^
  --skills 8 --diversity-coef 0 --redo-every 20 ^
  --init-from rl\checkpoints\league_cap_final.pt --checkpoint rl\checkpoints\cap_mirror.pt ^
  --logdir rl\runs\cap_mirror %* >> rl\checkpoints\cap_mirror.log 2>&1
