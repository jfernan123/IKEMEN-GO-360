@echo off
rem Sanity check for varied play (skills): Captain America from scratch against SmartAI Captain America, the matchup
rem that was learned fastest, with 8 skills, the 32-decision style window and a 0.01 style bonus. If the styles
rem separate here (diversity/discriminator_accuracy well above 12.5%%), the mechanism works.
setlocal
cd /d "%~dp0"
rl\.venv\Scripts\python.exe rl\train_pool.py --exe build\rl\mugen360.exe --game "DC vs MARVEL 2025" ^
  --p1 chars/captainamerica/captainamerica.def --p2 chars/captainamerica/captainamerica.def --stage stages/gotham.def ^
  --fights 512 --rollout 64 --size large --steps 30000000 --entropy-coef 0.01 ^
  --skills 8 --diversity-coef 0.01 --diversity-min-win 0.6 --style-window 32 ^
  --checkpoint rl\checkpoints\styles_sanity_cap_mirror.pt %*
