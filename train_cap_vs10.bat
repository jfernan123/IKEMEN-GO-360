@echo off
rem Captain America (always player 1, the policy) against SmartAI playing a random one of the ten roster10 characters
rem on Gotham. Same settings as train_roster10.bat, starting from the Cap sanity policy, to see whether one character
rem against many opponents learns where ten characters at once didn't. Pass --resume to continue.
setlocal
cd /d "%~dp0"
set "POOL=chars/captainamerica/captainamerica.def,chars/spiderman/spiderman.def,chars/INCREDIBLEHULK/INCREDIBLEHULK.def"
set "POOL=%POOL%,chars/Superman/Superman.def,chars/BatmanNew/BatmanNew.def,chars/Joker/Joker.def,chars/thanos/thanos.def"
set "POOL=%POOL%,chars/darkseid/darkseid.def,chars/doom/doom.def,chars/mvci venom/mvci venom.def"
rl\.venv\Scripts\python.exe rl\train_ppo.py --exe build\rl\mugen360.exe --game "DC vs MARVEL 2025" ^
  --p1 chars/captainamerica/captainamerica.def --characters "%POOL%" --stage stages/gotham.def --gamma 0.997 ^
  --envs 16 --rollout 512 --entropy-coef 0.003 --steps 3000000 --no-monitor ^
  --init-from rl\checkpoints\sanity_captainamerica.pt --checkpoint rl\checkpoints\cap_vs10.pt ^
  --replay-dir rl\wins_cap_vs10 %*
