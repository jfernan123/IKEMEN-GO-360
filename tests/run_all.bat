@echo off
setlocal
rem "run_all.bat show" plays every test in a visible window instead of hidden.
set "SHOW="
if /i "%~1"=="show" set "SHOW=--show"
cd /d "%~dp0\.."
if not exist tests\out mkdir tests\out
rem MUGEN360_BUILD=pc_test runs the second copy (build_pc_test.bat) while build\pc\mugen360.exe is open.
if not defined MUGEN360_BUILD set "MUGEN360_BUILD=pc"
set "EXE=build\%MUGEN360_BUILD%\mugen360.exe"
if not exist "%EXE%" (
  echo FAIL: %EXE% is missing. Run build_pc.bat first.
  exit /b 1
)

echo === Core and milestone unit tests ===
build\%MUGEN360_BUILD%\mugen360_tests.exe || exit /b 1

echo === Scripted gameplay integration tests ===
call :run attacks 1000 --quickstart || exit /b 1
call :run specials 900 --quickstart || exit /b 1
call :run hits 600 --quickstart || exit /b 1
call :guard || exit /b 1
call :run falls 700 --quickstart || exit /b 1
call :run effects 260 --quickstart --power 3000 || exit /b 1
call :reversal || exit /b 1
call :run cornerpush 400 --quickstart || exit /b 1
call :run taunt 100 --quickstart || exit /b 1
call :projectile || exit /b 1
call :projectile_clash || exit /b 1
call :helper || exit /b 1
call :misc || exit /b 1
call :select || exit /b 1
call :title || exit /b 1
call :run buttons 80 --quickstart || exit /b 1
call :run commands 260 --quickstart || exit /b 1
call :run states 330 --quickstart || exit /b 1
call :run match 7000 || exit /b 1
call :run timeover 6400 --quickstart || exit /b 1

findstr /c:"state 0 -> 3000" tests\out\latest_effects.log >nul || (echo FAIL: super state 3000 did not run & exit /b 1)
findstr /c:"superpause 30 ticks" tests\out\latest_effects.log >nul || (echo FAIL: SuperPause did not run & exit /b 1)
findstr /c:"afterimage length 20" tests\out\latest_effects.log >nul || (echo FAIL: super AfterImage did not run & exit /b 1)
findstr /c:"state 1300 -> 1310" tests\out\latest_reversal.log >nul || (echo FAIL: Kung Fu Blocking did not enter its success state & exit /b 1)
findstr /c:"reverses p2 state 200" tests\out\latest_reversal.log >nul || (echo FAIL: ReversalDef did not catch P2's punch & exit /b 1)
findstr /c:"cornerpush" tests\out\latest_cornerpush.log >nul || (echo FAIL: corner push was not applied & exit /b 1)
findstr /c:"-> 195" tests\out\latest_taunt.log >nul || (echo FAIL: Start did not reach KFM's taunt state & exit /b 1)
findstr /c:"projectile 7 hits for 50 damage" tests\out\latest_projectile.log >nul || (echo FAIL: Projectile did not hit P2 & exit /b 1)
findstr /c:"select: 3 characters" tests\out\latest_select.log >nul || (echo FAIL: the select screen did not load the roster & exit /b 1)
findstr /c:"select: p1 Kung Fu Man palette 1" tests\out\latest_select.log >nul || (echo FAIL: player 1 did not pick through the palette menu & exit /b 1)
findstr /c:"select: p2 Kung Fu Man palette 2" tests\out\latest_select.log >nul || (echo FAIL: player 1 did not pick player 2 & exit /b 1)
findstr /c:"select: stage Mountainside Temple" tests\out\latest_select.log >nul || (echo FAIL: the stage was not chosen & exit /b 1)
findstr /c:"versus: Kung Fu Man vs Kung Fu Man" tests\out\latest_select.log >nul || (echo FAIL: the VS screen did not show & exit /b 1)
findstr /c:"char: data/chars/kfm720/kfm720.def" tests\out\latest_select.log >nul || (echo FAIL: player 1's pick did not reach the fight & exit /b 1)
findstr /c:"match: round 1" tests\out\latest_select.log >nul || (echo FAIL: the fight did not start after the select screen & exit /b 1)
findstr /c:"title: VS MODE > 1P VS 2P" tests\out\latest_title.log >nul || (echo FAIL: the title menu did not pick 1P VS 2P & exit /b 1)
findstr /c:"music: can't open sound/Title.mp3" tests\out\latest_title.log >nul || (echo FAIL: the title did not ask for its music & exit /b 1)
findstr /c:"music: can't open sound/Versus.mp3" tests\out\latest_title.log >nul || (echo FAIL: the VS screen did not ask for its music & exit /b 1)
findstr /c:"music: can't open sound/kfm.mid" tests\out\latest_title.log >nul || (echo FAIL: the fight did not ask for the stage music & exit /b 1)
findstr /c:"versus: done at" tests\out\latest_title.log >nul || (echo FAIL: Start did not skip the VS screen & exit /b 1)
findstr /c:"match: round 1" tests\out\latest_title.log >nul || (echo FAIL: the fight did not start from the title menu & exit /b 1)
findstr /c:"state 1002 -> 1009" tests\out\latest_misc.log >nul || (echo FAIL: 7d controllers and triggers did not all check out & exit /b 1)
findstr /c:"helper 88 state 1200" tests\out\latest_helper.log >nul || (echo FAIL: nested Helper was not created & exit /b 1)
findstr /c:"state 1000 -> 1002" tests\out\latest_helper.log >nul || (echo FAIL: Helper parent/root redirects or NumHelper failed & exit /b 1)
findstr /c:"helper 77 destroyed" tests\out\latest_helper.log >nul || (echo FAIL: DestroySelf did not remove the helper & exit /b 1)
findstr /c:"projectile 8 clashes with p2 projectile 8" tests\out\latest_projectile_clash.log >nul || (echo FAIL: Projectiles did not cancel each other & exit /b 1)
for %%S in (200 210 230 240 400 410 430 440 600 610 630 640) do findstr /c:"-> %%S" tests\out\latest_attacks.log >nul || (echo FAIL: attack state %%S did not run & exit /b 1)
for %%S in (100 105 1000 1400 1100 800 810 820 821) do findstr /c:"-> %%S" tests\out\latest_specials.log >nul || (echo FAIL: special state %%S did not run & exit /b 1)
findstr /c:"hits: damage" tests\out\latest_hits.log >nul || (echo FAIL: hit test made no contact & exit /b 1)
findstr /c:"-> 150" tests\out\latest_guard.log >nul || (echo FAIL: standing guard did not run & exit /b 1)
findstr /c:"-> 152" tests\out\latest_guard.log >nul || (echo FAIL: crouching guard did not run & exit /b 1)
for %%S in (5070 5030 5050 5100 5110 5120) do findstr /c:"-> %%S" tests\out\latest_falls.log >nul || (echo FAIL: fall state %%S did not run & exit /b 1)
findstr /c:"explod F61" tests\out\latest_falls.log >nul || (echo FAIL: landing Explod did not run & exit /b 1)
findstr /c:"juggle cost" tests\out\latest_falls.log >nul || (echo FAIL: air juggle points were not consumed & exit /b 1)
findstr /c:"p1 command: c" tests\out\latest_buttons.log >nul || (echo FAIL: six-button input test changed & exit /b 1)
findstr /c:"p1 command: QCF_x" tests\out\latest_commands.log >nul || (echo FAIL: command input test changed & exit /b 1)
findstr /c:"-> 40" tests\out\latest_states.log >nul || (echo FAIL: jump state test changed & exit /b 1)
findstr /c:"time over, wins 1-0" tests\out\latest_timeover.log >nul || (echo FAIL: time-over result changed & exit /b 1)
findstr /c:"round 2: KO, wins 2-0" tests\out\latest_match.log >nul || (echo FAIL: full match result changed & exit /b 1)
findstr /c:"fightscreen: data/fight.def" tests\out\latest_match.log >nul || (echo FAIL: the fight.def fight screen did not load & exit /b 1)
findstr /c:"round 1 call, fight call" tests\out\latest_match.log >nul || (echo FAIL: the round and fight calls did not run & exit /b 1)
findstr /c:"fightscreen: KO call" tests\out\latest_match.log >nul || (echo FAIL: the KO call did not run & exit /b 1)
findstr /c:"fightscreen: TO call" tests\out\latest_timeover.log >nul || (echo FAIL: the time over call did not run & exit /b 1)

echo PASS: all unit and scripted integration tests passed.
exit /b 0

:run
echo     %1
"%EXE%" --input "tests\inputs\%1.txt" --quit %2 --trace %SHOW% %3 %4 %5 >nul || exit /b 1
copy /y m360.log "tests\out\latest_%1.log" >nul
exit /b 0

:guard
echo     guard
"%EXE%" --input tests\inputs\guard.txt --input2 tests\inputs\guard_p2.txt --quickstart --quit 460 --trace %SHOW% >nul || exit /b 1
copy /y m360.log tests\out\latest_guard.log >nul
exit /b 0

:reversal
echo     reversal
"%EXE%" --input tests\inputs\reversal.txt --input2 tests\inputs\reversal_p2.txt --quickstart --quit 180 --trace %SHOW% >nul || exit /b 1
copy /y m360.log tests\out\latest_reversal.log >nul
exit /b 0

:projectile
echo     projectile
"%EXE%" --p1 tests/fixtures/projectile/projectile.def --input tests/inputs/projectile.txt --quickstart --quit 120 --trace %SHOW% >nul || exit /b 1
copy /y m360.log tests\out\latest_projectile.log >nul
exit /b 0

:projectile_clash
echo     projectile clash
"%EXE%" --p1 tests/fixtures/projectile/projectile.def --p2 tests/fixtures/projectile/projectile.def --input tests/inputs/projectile_clash.txt --input2 tests/inputs/projectile_p2.txt --quickstart --quit 120 --trace %SHOW% >nul || exit /b 1
copy /y m360.log tests\out\latest_projectile_clash.log >nul
exit /b 0

:helper
echo     helper
"%EXE%" --p1 tests/fixtures/helper/helper.def --input tests/inputs/helper.txt --quickstart --quit 140 --trace %SHOW% >nul || exit /b 1
copy /y m360.log tests\out\latest_helper.log >nul
exit /b 0

:select
echo     select screen
"%EXE%" --select --cpu --input tests/inputs/select.txt --quit 600 --trace %SHOW% >nul || exit /b 1
copy /y m360.log tests\out\latest_select.log >nul
exit /b 0

:title
echo     title menu
"%EXE%" --title --cpu --input tests/inputs/title.txt --quit 600 --trace %SHOW% >nul || exit /b 1
copy /y m360.log tests\out\latest_title.log >nul
exit /b 0

:misc
echo     misc controllers
"%EXE%" --p1 tests/fixtures/misc/misc.def --input tests/inputs/misc.txt --quickstart --quit 60 --trace %SHOW% >nul || exit /b 1
copy /y m360.log tests\out\latest_misc.log >nul
exit /b 0
