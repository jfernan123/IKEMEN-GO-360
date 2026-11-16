@echo off
rem DC vs MARVEL 2025 with the from-scratch neural Captain America at 491M (the one train_cap_thanos.bat starts from) in
rem VS NEURAL CAP: it plays player 1, you pick your fighter from its opponents and play player 2. Kept out of git.
cd /d "%~dp0"
copy /y data\neural\neural_cap_scratch491.ini data\neural\neural.ini > nul
build\pc\mugen360.exe --game "DC vs MARVEL 2025"
