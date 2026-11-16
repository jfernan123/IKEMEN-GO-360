@echo off
rem The general Cap from scratch again, now with all 65 button choices from the first decision: the recipe of
rem train_cap_scratch.bat (mirror 384 fights, SmartAI 128, 600M), plus a network copy every 100 updates.
rem   train_cap_scratch65.bat <cores to use> [more options, such as --resume]
rem Log: rl\checkpoints\cap_scratch65.log. TensorBoard: rl\runs\cap_scratch65\.
setlocal
cd /d "%~dp0"
set "CORES=%~1"
set "OPP=chars/thanos/thanos.def,chars/mvci venom/mvci venom.def,chars/doom/doom.def,chars/darkseid/darkseid.def"
set "OPP=%OPP%,chars/nightwing/nightwing.def"
rl\.venv\Scripts\python.exe rl\train_pool.py --exe build\rl_new\mugen360.exe --game "DC vs MARVEL 2025" ^
  --p1 chars/captainamerica/captainamerica.def --p2 "%OPP%" --stage stages/gotham.def ^
  --fights 512 --league 384 --pools 2 --cores %CORES% --random-sides --league-self 0.35 --snapshot-every 300 ^
  --league-size 16 --check-every 150 --check-matches 64 --keep-every 100 ^
  --rollout 64 --size large --steps 600000000 --entropy-coef 0.003 --lr-max 1e-3 ^
  --damage-dealt-weight 0 --damage-taken-weight 0 --gamma 0.9995 --win-time-cost 0.0004 --time-over loss ^
  --redo-every 20 --checkpoint rl\checkpoints\cap_scratch65.pt --logdir rl\runs\cap_scratch65 %2 ^
  >> rl\checkpoints\cap_scratch65.log 2>&1
