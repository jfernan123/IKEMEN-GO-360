@echo off
rem Captain America's self-play league (AlphaStar-style, scaled down), from the 5-opponent styles policy:
rem   256 of 512 fights are Cap vs Cap, player 2 played by the current policy (35%) or a frozen snapshot picked by
rem   PFSP (the ones it loses to most come up most); a snapshot joins every 300 updates (~10M decisions), 16 kept.
rem   The other 256 fights stay against SmartAI Thanos, Venom, Doom, Darkseid and Nightwing, so it can't drift.
rem   Same as pool_cap_vs5_skills otherwise: 8 skills, ReDo, the Thanos anchor, entropy 0.003.
rem Snapshots go to rl\checkpoints\league_cap_league\. Pass --resume to continue rl\checkpoints\league_cap.pt.
setlocal
cd /d "%~dp0"
set "OPP=chars/thanos/thanos.def,chars/mvci venom/mvci venom.def,chars/doom/doom.def,chars/darkseid/darkseid.def"
set "OPP=%OPP%,chars/nightwing/nightwing.def"
rl\.venv\Scripts\python.exe rl\train_pool.py --exe build\rl_new\mugen360.exe --game "DC vs MARVEL 2025" ^
  --p1 chars/captainamerica/captainamerica.def --p2 "%OPP%" --stage stages/gotham.def ^
  --fights 512 --league 256 --league-self 0.35 --snapshot-every 300 --league-size 16 ^
  --rollout 64 --size large --steps 300000000 --entropy-coef 0.003 ^
  --anchor rl\checkpoints\cap_thanos_specialist.pt --anchor-opponents chars/thanos/thanos.def --anchor-coef 0.5 ^
  --skills 8 --diversity-coef 0.001 --diversity-min-win 0.6 --redo-every 20 ^
  --init-from rl\checkpoints\pool_cap_vs5_skills.pt --checkpoint rl\checkpoints\league_cap.pt %*
