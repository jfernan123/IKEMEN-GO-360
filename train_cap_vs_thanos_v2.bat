@echo off
rem Captain America against SmartAI Thanos, starting from the first Thanos run's policy (widened to the new inputs), on Gotham, with the double jump, helpers and projectiles in the
rem observation, and sequence (BPTT) training for the GRU. Needs build_rl.bat run after those changes. Pass --resume
rem to continue rl\checkpoints\cap_vs_thanos_v2.pt.
setlocal
cd /d "%~dp0"
rl\.venv\Scripts\python.exe rl\train_ppo.py --exe build\rl\mugen360.exe --game "DC vs MARVEL 2025" ^
  --p1 chars/captainamerica/captainamerica.def --p2 chars/thanos/thanos.def --stage stages/gotham.def ^
  --gamma 0.997 --envs 16 --rollout 512 --entropy-coef 0.01 --bptt 32 --lr 1e-3 --minibatch 512 --steps 12000000 --no-monitor ^
  --init-from rl\checkpoints\cap_vs_thanos.pt --checkpoint rl\checkpoints\cap_vs_thanos_v2.pt --replay-dir rl\wins_cap_vs_thanos_v2 %*
