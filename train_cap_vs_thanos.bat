@echo off
rem Captain America from scratch (no starting checkpoint), only against SmartAI Thanos on Gotham: the matchup the
rem Cap-vs-10 policy couldn't win (2%%). Pass --resume to continue rl\checkpoints\cap_vs_thanos.pt.
setlocal
cd /d "%~dp0"
rl\.venv\Scripts\python.exe rl\train_ppo.py --exe build\rl\mugen360.exe --game "DC vs MARVEL 2025" ^
  --p1 chars/captainamerica/captainamerica.def --p2 chars/thanos/thanos.def --stage stages/gotham.def ^
  --gamma 0.997 --envs 16 --rollout 512 --entropy-coef 0.01 --steps 3000000 --no-monitor ^
  --checkpoint rl\checkpoints\cap_vs_thanos.pt --replay-dir rl\wins_cap_vs_thanos %*
