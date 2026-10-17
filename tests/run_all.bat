@echo off
setlocal
cd /d "%~dp0\.."
set "EXE=build\pc\mugen360.exe"
if not exist "%EXE%" (
  echo FAIL: %EXE% is missing. Run build_pc.bat first.
  exit /b 1
)

echo === Core and milestone unit tests ===
build\pc\mugen360_tests.exe || exit /b 1

echo === Scripted gameplay integration tests ===
call :run attacks 1000 --quickstart || exit /b 1
call :run specials 900 --quickstart || exit /b 1
call :run hits 600 --quickstart || exit /b 1
call :guard || exit /b 1
call :run falls 700 --quickstart || exit /b 1
call :run effects 260 --quickstart --power 3000 || exit /b 1
call :run buttons 80 --quickstart || exit /b 1
call :run commands 260 --quickstart || exit /b 1
call :run states 330 --quickstart || exit /b 1
call :run match 7000 || exit /b 1
call :run timeover 6400 --quickstart || exit /b 1

findstr /c:"state 0 -> 3000" tests\out\latest_effects.log >nul || (echo FAIL: super state 3000 did not run & exit /b 1)
findstr /c:"superpause 30 ticks" tests\out\latest_effects.log >nul || (echo FAIL: SuperPause did not run & exit /b 1)
findstr /c:"afterimage length 20" tests\out\latest_effects.log >nul || (echo FAIL: super AfterImage did not run & exit /b 1)
for %%S in (200 210 230 240 400 410 430 440 600 610 630 640) do findstr /c:"-> %%S" tests\out\latest_attacks.log >nul || (echo FAIL: attack state %%S did not run & exit /b 1)
for %%S in (100 105 1000 1400 1100 800 810 820 821) do findstr /c:"-> %%S" tests\out\latest_specials.log >nul || (echo FAIL: special state %%S did not run & exit /b 1)
findstr /c:"hits: damage" tests\out\latest_hits.log >nul || (echo FAIL: hit test made no contact & exit /b 1)
findstr /c:"-> 150" tests\out\latest_guard.log >nul || (echo FAIL: standing guard did not run & exit /b 1)
findstr /c:"-> 152" tests\out\latest_guard.log >nul || (echo FAIL: crouching guard did not run & exit /b 1)
for %%S in (5070 5030 5050 5100 5110 5120) do findstr /c:"-> %%S" tests\out\latest_falls.log >nul || (echo FAIL: fall state %%S did not run & exit /b 1)
findstr /c:"explod F61" tests\out\latest_falls.log >nul || (echo FAIL: landing Explod did not run & exit /b 1)
findstr /c:"p1 command: c" tests\out\latest_buttons.log >nul || (echo FAIL: six-button input test changed & exit /b 1)
findstr /c:"p1 command: QCF_x" tests\out\latest_commands.log >nul || (echo FAIL: command input test changed & exit /b 1)
findstr /c:"-> 40" tests\out\latest_states.log >nul || (echo FAIL: jump state test changed & exit /b 1)
findstr /c:"time over, wins 1-0" tests\out\latest_timeover.log >nul || (echo FAIL: time-over result changed & exit /b 1)
findstr /c:"round 2: KO, wins 2-0" tests\out\latest_match.log >nul || (echo FAIL: full match result changed & exit /b 1)

echo PASS: all unit and scripted integration tests passed.
exit /b 0

:run
echo     %1
"%EXE%" --input "tests\inputs\%1.txt" --quit %2 --trace %3 %4 %5 >nul || exit /b 1
copy /y m360.log "tests\out\latest_%1.log" >nul
exit /b 0

:guard
echo     guard
"%EXE%" --input tests\inputs\guard.txt --input2 tests\inputs\guard_p2.txt --quickstart --quit 460 --trace >nul || exit /b 1
copy /y m360.log tests\out\latest_guard.log >nul
exit /b 0
