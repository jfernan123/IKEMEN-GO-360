@echo off
rem Captain America from scratch against SmartAI Thanos on Gotham with the large (deeper) network: three 256-wide
rem layers with skip connections, a 256 GRU and two-layer heads. Same setup as train_cap_vs_thanos_v2.bat otherwise
rem (double jump, helpers and projectiles observed, BPTT 32), so the two can be compared. Pass --resume to continue.
setlocal
cd /d "%~dp0"
rl\.venv\Scripts\python.exe rl\train_ppo.py --exe build\rl\mugen360.exe --game "DC vs MARVEL 2025" ^
  --p1 chars/captainamerica/captainamerica.def --p2 chars/thanos/thanos.def --stage stages/gotham.def ^
  --size large --gamma 0.997 --envs 16 --rollout 512 --entropy-coef 0.01 --bptt 32 --lr 1e-3 --target-kl 0.02 --minibatch 512 ^
  --steps 12000000 --no-monitor --checkpoint rl\checkpoints\cap_vs_thanos_large.pt --replay-dir rl\wins_cap_vs_thanos_large %*
