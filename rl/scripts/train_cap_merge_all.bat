@echo off
rem The merge of every specialist (rl\PIPELINE.md, stage 3, for the whole roster): a fresh Cap learns from one teacher
rem per opponent while earning the same reward the specialists did, so he can pass them. Same recipe as
rem train_cap_merge.bat (the first six), except that the opponents take turns: 16 at a time, 32 fights each; every 50
rem updates (about 1.6M decisions) up to half stay, the ones he trails his teacher most against, and the ones waiting
rem longest come in. Each teacher's win rate comes from rl\pick_teachers.py --all, which writes
rem rl\merge_teachers_all.txt; rl\scratch_queue.py runs both once the last specialist finishes.
rem   train_cap_merge_all.bat [more options, such as --resume]
rem Log: rl\checkpoints\cap_merged_all.log. TensorBoard: rl\runs\cap_merged_all\. A copy every 100 updates in
rem rl\checkpoints\cap_merged_all_history\.
setlocal
cd /d "%~dp0..\.."
if not exist rl\merge_teachers_all.txt (
  echo Run rl\pick_teachers.py --all --copies 3 first.
  exit /b 1
)
rl\.venv\Scripts\python.exe rl\train_pool.py --exe build\rl\mugen360.exe --game "DC vs MARVEL 2025" ^
  --p1 chars/captainamerica/captainamerica.def --p2 teachers --teachers rl\merge_teachers_all.txt ^
  --stage stages/gotham.def --active 16 --rotate-every 50 --min-share 0 ^
  --fights 512 --cores 16 --check-every 300 --check-matches 64 --keep-every 100 ^
  --rollout 64 --size large --steps 1500000000 --entropy-coef 0.003 --lr 3e-4 --lr-max 3e-4 --target-kl 0 ^
  --damage-dealt-weight 0 --damage-taken-weight 0 --gamma 1.0 --win-time-cost 0.0004 --time-over loss ^
  --redo-every 20 --checkpoint rl\checkpoints\cap_merged_all.pt --logdir rl\runs\cap_merged_all %* ^
  >> rl\checkpoints\cap_merged_all.log 2>&1
