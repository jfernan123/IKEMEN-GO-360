@echo off
rem Captain America against SmartAI Thanos, Venom, Doctor Doom, Darkseid and Nightwing at once: 512 fights split
rem evenly, one RL pool per opponent, starting from the Thanos specialist (rl\checkpoints\cap_thanos_specialist.pt).
rem Pass --resume to continue rl\checkpoints\pool_cap_vs5.pt.
setlocal
cd /d "%~dp0"
set "OPP=chars/thanos/thanos.def,chars/mvci venom/mvci venom.def,chars/doom/doom.def,chars/darkseid/darkseid.def"
set "OPP=%OPP%,chars/nightwing/nightwing.def"
rl\.venv\Scripts\python.exe rl\train_pool.py --exe build\rl\mugen360.exe --game "DC vs MARVEL 2025" ^
  --p1 chars/captainamerica/captainamerica.def --p2 "%OPP%" --stage stages/gotham.def ^
  --fights 512 --rollout 64 --size large --steps 300000000 ^
  --init-from rl\checkpoints\cap_thanos_specialist.pt --checkpoint rl\checkpoints\pool_cap_vs5.pt %*
