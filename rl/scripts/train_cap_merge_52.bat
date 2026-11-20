@echo off
rem The merge of the 52 Cap specialists that reached 95% twice in a row (rl\merge_teachers_52.txt), 2026-11-20: the
rem recipe of train_cap_merge_all.bat (fresh student, 16 opponents at a time, rotating every 50 updates) on the
rem specialists finished so far. Each teacher's win rate is measured (256 matches) the first time it comes up.
rem 450M decisions: the six-opponent merge came within reach of its teachers at about 8M decisions per opponent.
rem   train_cap_merge_52.bat [more options, such as --resume]
rem Log: rl\checkpoints\cap_merged_52.log. TensorBoard: rl\runs\cap_merged_52\. A copy every 100 updates in
rem rl\checkpoints\cap_merged_52_history\.
setlocal
cd /d "%~dp0..\.."
rl\.venv\Scripts\python.exe rl\train_pool.py --exe build\rl\mugen360.exe --game "DC vs MARVEL 2025" ^
  --p1 chars/captainamerica/captainamerica.def --p2 teachers --teachers rl\merge_teachers_52.txt ^
  --stage stages/gotham.def --active 16 --rotate-every 50 --min-share 0 ^
  --fights 512 --cores 16 --check-every 300 --check-matches 64 --keep-every 100 ^
  --rollout 64 --size large --steps 450000000 --entropy-coef 0.003 --lr 3e-4 --lr-max 3e-4 --target-kl 0 ^
  --damage-dealt-weight 0 --damage-taken-weight 0 --gamma 1.0 --win-time-cost 0.0004 --time-over loss ^
  --redo-every 20 --checkpoint rl\checkpoints\cap_merged_52.pt --logdir rl\runs\cap_merged_52 %* ^
  >> rl\checkpoints\cap_merged_52.log 2>&1
