@echo off
rem Captain America from scratch against one SmartAI opponent: a fresh network that sees only this character, the
rem teacher for one opponent in the merge (rl\PIPELINE.md). Same recipe as train_cap_thanos.bat, without --init-from.
rem +1 a win, -1 a loss, no damage terms, time over a loss, a win loses 0.0004 per decision, no discount, all 65 button
rem choices from the first decision. The opponent starts with 3% of his life bar and gets more when Cap wins 60% of 200
rem matches at the current size, less when he wins under 20%. A copy every 100 updates in
rem rl\checkpoints\scratch_vs_<name>_history\: take the best one, not the last (cap_thanos collapsed after its peak).
rem Stops early once two checks in a row win 95%, against the full life bar; 200M steps is the limit otherwise.
rem   train_cap_specialist.bat <name> <opponent's def> <threads per pool> [more options, such as --resume]
rem Log: rl\checkpoints\scratch_vs_<name>.log. TensorBoard: rl\runs\scratch_vs_<name>\.
setlocal
cd /d "%~dp0"
set "NAME=%~1"
set "OPPONENT=%~2"
set "THREADS=%~3"
rem Galactus fights on his own stage, as select.def pairs them and his readme asks.
set "STAGE=stages/gotham.def"
if "%NAME%"=="galactus" set "STAGE=stages/GalactusOutpost.def"
rl\.venv\Scripts\python.exe rl\train_pool.py --exe build\rl_fix\mugen360.exe --game "DC vs MARVEL 2025" ^
  --p1 chars/captainamerica/captainamerica.def --p2 "%OPPONENT%" --stage %STAGE% ^
  --fights 512 --pools 2 --threads %THREADS% --check-every 150 --check-matches 64 --keep-every 100 ^
  --curriculum 0.03,0.06,0.1,0.15,0.2,0.3,0.4,0.5,0.65,0.8,1 --curriculum-win 0.6 --curriculum-back 0.2 ^
  --rollout 64 --size large --steps 200000000 --stop-win 0.95 --entropy-coef 0.01 --lr 3e-4 --lr-max 3e-4 ^
  --damage-dealt-weight 0 --damage-taken-weight 0 --gamma 1.0 --win-time-cost 0.0004 --time-over loss ^
  --redo-every 20 --checkpoint rl\checkpoints\scratch_vs_%NAME%.pt --logdir rl\runs\scratch_vs_%NAME% %4 ^
  >> rl\checkpoints\scratch_vs_%NAME%.log 2>&1
