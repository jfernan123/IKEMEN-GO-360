@echo off
rem The merge (rl\PIPELINE.md, stage 3): a fresh Cap learns from one teacher per opponent while earning the same reward as
rem the specialists, so he can pass them. Run rl\pick_teachers.py first: it writes rl\merge_teachers.txt, and each
rem teacher's pull fades as Cap comes within 5 points of it. Six opponents: only the specialists' characters.
rem   train_cap_merge.bat [more options, such as --resume]
rem Log: rl\checkpoints\cap_merged.log. TensorBoard: rl\runs\cap_merged\. A copy every 100 updates in
rem rl\checkpoints\cap_merged_history\.
setlocal
cd /d "%~dp0"
if not exist rl\merge_teachers.txt (
  echo Run rl\pick_teachers.py first.
  exit /b 1
)
rl\.venv\Scripts\python.exe rl\train_pool.py --exe build\rl_fix\mugen360.exe --game "DC vs MARVEL 2025" ^
  --p1 chars/captainamerica/captainamerica.def ^
  --p2 "chars/thanos/thanos.def,chars/doomsdayCG/doomsdayCG.def,chars/captainamerica/captainamerica.def,chars/GALACTUS/GALACTUS.def,chars/Superman/Superman.def,chars/darkseid/darkseid.def" ^
  --teachers rl\merge_teachers.txt --stage stages/gotham.def ^
  --fights 512 --cores 16 --reweight-every 50 --min-share 0.04 --check-every 300 --check-matches 64 --keep-every 100 ^
  --rollout 64 --size large --steps 300000000 --entropy-coef 0.003 --lr 3e-4 --lr-max 3e-4 --target-kl 0 ^
  --damage-dealt-weight 0 --damage-taken-weight 0 --gamma 1.0 --win-time-cost 0.0004 --time-over loss ^
  --redo-every 20 --checkpoint rl\checkpoints\cap_merged.pt --logdir rl\runs\cap_merged %* ^
  >> rl\checkpoints\cap_merged.log 2>&1
