@echo off
rem Captain America against Thanos, Venom, Doctor Doom, Darkseid and Nightwing, from the 5-opponent policy, with:
rem   8 skills (SMERL-style varied play: a style bonus only against opponents already beaten 60%+ of the time),
rem   ReDo every 20 updates (dormant units recycled), the Thanos anchor and entropy 0.003.
rem Pass --resume to continue rl\checkpoints\pool_cap_vs5_skills.pt.
setlocal
cd /d "%~dp0"
set "OPP=chars/thanos/thanos.def,chars/mvci venom/mvci venom.def,chars/doom/doom.def,chars/darkseid/darkseid.def"
set "OPP=%OPP%,chars/nightwing/nightwing.def"
rl\.venv\Scripts\python.exe rl\train_pool.py --exe build\rl\mugen360.exe --game "DC vs MARVEL 2025" ^
  --p1 chars/captainamerica/captainamerica.def --p2 "%OPP%" --stage stages/gotham.def ^
  --fights 512 --rollout 64 --size large --steps 300000000 --entropy-coef 0.003 ^
  --anchor rl\checkpoints\cap_thanos_specialist.pt --anchor-opponents chars/thanos/thanos.def --anchor-coef 0.5 ^
  --skills 8 --diversity-coef 0.001 --diversity-min-win 0.6 --redo-every 20 ^
  --init-from rl\checkpoints\pool_cap_vs5_start_skills.pt --checkpoint rl\checkpoints\pool_cap_vs5_skills.pt %*
