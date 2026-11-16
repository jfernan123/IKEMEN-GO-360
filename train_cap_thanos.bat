@echo off
rem Captain America against SmartAI Thanos only, starting from the from-scratch Cap at 491M
rem (rl\checkpoints\cap_scratch_league\snap_000491520000.pt), who never beat Thanos. Reward: +1 a win, -1 a loss, no
rem damage terms, time over a loss, a win loses 0.0004 per decision it took. No discount (gamma 1): a loss counts the
rem same whenever it comes, so putting it off (blocking all match) doesn't pay. The second try (gamma 0.9995, lr 1e-3)
rem collapsed at 50% into holding back every decision.
rem Button choices widened from 8 to 65 (presses of several buttons at once, for hypers) when the run starts: the new
rem ones begin at ~1.7% of his choices, so he plays as before and learns them. All 512 fights are Thanos, in 2 pools;
rem Cap plays player 1. Thanos starts with 3% of his life bar and gets more each time Cap wins 60% of 200 matches at
rem the current size, and goes back a step when Cap wins less than 20% there. Entropy 0.01, lr 3e-4 at most.
rem A copy of the network every 100 updates in rl\checkpoints\cap_thanos_history\. Checked against full-life Thanos
rem (64 matches) every 150 updates.
rem Pass --resume to continue rl\checkpoints\cap_thanos.pt. Log: rl\checkpoints\cap_thanos.log. TensorBoard:
rem rl\runs\cap_thanos\.
setlocal
cd /d "%~dp0"
rl\.venv\Scripts\python.exe rl\train_pool.py --exe build\rl_new\mugen360.exe --game "DC vs MARVEL 2025" ^
  --p1 chars/captainamerica/captainamerica.def --p2 chars/thanos/thanos.def --stage stages/gotham.def ^
  --fights 512 --pools 2 --check-every 150 --check-matches 64 --keep-every 100 ^
  --curriculum 0.03,0.06,0.1,0.15,0.2,0.3,0.4,0.5,0.65,0.8,1 --curriculum-win 0.6 --curriculum-back 0.2 ^
  --rollout 64 --size large --steps 200000000 --entropy-coef 0.01 --lr 3e-4 --lr-max 3e-4 ^
  --damage-dealt-weight 0 --damage-taken-weight 0 --gamma 1.0 --win-time-cost 0.0004 --time-over loss ^
  --redo-every 20 --init-from rl\checkpoints\cap_scratch_league\snap_000491520000.pt ^
  --checkpoint rl\checkpoints\cap_thanos.pt --logdir rl\runs\cap_thanos %* >> rl\checkpoints\cap_thanos.log 2>&1