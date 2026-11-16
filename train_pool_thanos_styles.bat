@echo off
rem Eight styles of Captain America against SmartAI Thanos, trained from scratch (the styles form while he learns, as
rem in the Cap-mirror sanity run). A style bonus (0.002, judged over 32 decisions) applies once he beats Thanos 60%%+
rem of the time. No ReDo (it kept
rem resetting the third layer while training from scratch). Style codes start at 0.01 of their usual size, so all 8
rem start as one policy and learn as fast as a run without styles (big random codes made it 15-20x slower).
rem Curriculum: Thanos starts with 25%% of his life bar, then 50%%, 75%%, 100%%, each step once Cap wins 60%% there. Pass --resume to continue rl\checkpoints\cap_thanos_styles.pt.
setlocal
cd /d "%~dp0"
rl\.venv\Scripts\python.exe rl\train_pool.py --exe build\rl\mugen360.exe --game "DC vs MARVEL 2025" ^
  --p1 chars/captainamerica/captainamerica.def --p2 chars/thanos/thanos.def --stage stages/gotham.def ^
  --fights 512 --rollout 64 --size large --steps 200000000 --entropy-coef 0.01 ^
  --curriculum 0.25,0.5,0.75,1 --skills 8 --skill-scale 0.01 --diversity-coef 0.002 --diversity-min-win 0.6 --style-window 32 ^
  --checkpoint rl\checkpoints\cap_thanos_styles.pt %*
