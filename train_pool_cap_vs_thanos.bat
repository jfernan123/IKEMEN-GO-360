@echo off
rem Captain America against SmartAI Thanos on Gotham with the RL pool: 512 fights in two mugen360 processes, the deep
rem (large) network, sequence training and an adaptive learning rate. About 10x the speed of train_ppo.py.
rem Needs build_rl.bat run after the pool was added. Pass --resume to continue rl\checkpoints\pool_cap_vs_thanos.pt.
setlocal
cd /d "%~dp0"
rl\.venv\Scripts\python.exe rl\train_pool.py --exe build\rl\mugen360.exe --game "DC vs MARVEL 2025" ^
  --p1 chars/captainamerica/captainamerica.def --p2 chars/thanos/thanos.def --stage stages/gotham.def ^
  --fights 512 --pools 2 --rollout 64 --size large --steps 50000000 ^
  --checkpoint rl\checkpoints\pool_cap_vs_thanos.pt %*
