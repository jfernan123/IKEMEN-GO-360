@echo off
rem Ten characters SmartAI plays well (rl/smartai_scan.md), random pairs from them on Gotham. The policy is player 1,
rem SmartAI controls player 2. A new run starts from the trained Captain America sanity policy; pass --resume to
rem continue rl\checkpoints\roster10.pt. Needs the optimized engine from build_rl.bat.
setlocal
cd /d "%~dp0"
set "POOL=chars/captainamerica/captainamerica.def,chars/spiderman/spiderman.def,chars/INCREDIBLEHULK/INCREDIBLEHULK.def"
set "POOL=%POOL%,chars/Superman/Superman.def,chars/BatmanNew/BatmanNew.def,chars/Joker/Joker.def,chars/thanos/thanos.def"
set "POOL=%POOL%,chars/darkseid/darkseid.def,chars/doom/doom.def,chars/mvci venom/mvci venom.def"
rl\.venv\Scripts\python.exe rl\train_ppo.py --exe build\rl\mugen360.exe --game "DC vs MARVEL 2025" --characters "%POOL%" ^
  --stage stages/gotham.def --gamma 0.997 --envs 16 --rollout 512 --entropy-coef 0.003 --steps 10000000 --no-monitor ^
  --init-from rl\checkpoints\sanity_captainamerica.pt --checkpoint rl\checkpoints\roster10.pt ^
  --replay-dir rl\wins_roster10 %*
