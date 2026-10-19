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
call :training || exit /b 1
call :reversal || exit /b 1
call :run cornerpush 400 --quickstart || exit /b 1
call :run taunt 100 --quickstart || exit /b 1
call :projectile || exit /b 1
call :projectile_clash || exit /b 1
call :helper || exit /b 1
call :assist || exit /b 1
call :misc || exit /b 1
call :select || exit /b 1
call :title || exit /b 1
call :arcade || exit /b 1
call :team || exit /b 1
call :survival || exit /b 1
call :simul || exit /b 1
call :watch || exit /b 1
call :keys || exit /b 1
call :game || exit /b 1
call :options || exit /b 1
call :run buttons 80 --quickstart || exit /b 1
call :run commands 260 --quickstart || exit /b 1
call :run states 330 --quickstart || exit /b 1
call :run match 7000 || exit /b 1
call :run timeover 6400 --quickstart || exit /b 1

findstr /c:"state 0 -> 3000" tests\out\latest_effects.log >nul || (echo FAIL: super state 3000 did not run & exit /b 1)
findstr /c:"superpause 30 ticks" tests\out\latest_effects.log >nul || (echo FAIL: SuperPause did not run & exit /b 1)
findstr /c:"afterimage length 20" tests\out\latest_effects.log >nul || (echo FAIL: super AfterImage did not run & exit /b 1)
findstr /c:"training restore life" tests\out\latest_training.log >nul || (echo FAIL: Training did not restore hit damage & exit /b 1)
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
findstr /c:"title: ARCADE > SINGLE MODE" tests\out\latest_arcade.log >nul || (echo FAIL: ARCADE did not start from the title & exit /b 1)
findstr /c:"arcade: match 1 of 2" tests\out\latest_arcade.log >nul || (echo FAIL: Arcade did not build its roster route & exit /b 1)
findstr /c:"versus: done at" tests\out\latest_arcade.log >nul || (echo FAIL: Arcade did not show its VS screen & exit /b 1)
findstr /c:"match: round 1" tests\out\latest_arcade.log >nul || (echo FAIL: Arcade did not start its first match & exit /b 1)
findstr /c:"title: VS MODE > TEAM VERSUS" tests\out\latest_team.log >nul || (echo FAIL: TEAM VERSUS did not start from the title & exit /b 1)
findstr /c:"select: p1 team Turns" tests\out\latest_team.log >nul || (echo FAIL: player 1 did not choose Turns in the team menu & exit /b 1)
findstr /c:"select: p2 team Turns" tests\out\latest_team.log >nul || (echo FAIL: player 2 did not choose Turns in the team menu & exit /b 1)
findstr /c:"select: p1 member 2" tests\out\latest_team.log >nul || (echo FAIL: player 1 did not select two team members & exit /b 1)
findstr /c:"select: p2 member 2" tests\out\latest_team.log >nul || (echo FAIL: player 2 did not select two team members & exit /b 1)
findstr /c:"turns: p1 member 1 of 2 vs p2 member 1 of 2" tests\out\latest_team.log >nul || (echo FAIL: Turns did not start its first pairing & exit /b 1)
findstr /c:"turns: p1 member 1 of 2 vs p2 member 2 of 2" tests\out\latest_team.log >nul || (echo FAIL: Turns did not advance the losing side's next member & exit /b 1)
findstr /c:"turns: p1 recovers" tests\out\latest_team.log >nul || (echo FAIL: Turns did not preserve and recover the winning member's life & exit /b 1)
findstr /c:"select: SURVIVAL" tests\out\latest_survival.log >nul || (echo FAIL: Survival did not open its select screen & exit /b 1)
findstr /c:"survival: match 1" tests\out\latest_survival.log >nul || (echo FAIL: Survival did not start its first match & exit /b 1)
findstr /c:"survival: player advances" tests\out\latest_survival.log >nul || (echo FAIL: Survival did not advance after a win & exit /b 1)
findstr /c:"survival: match 2" tests\out\latest_survival.log >nul || (echo FAIL: Survival did not start its second match & exit /b 1)
findstr /c:"game: tests/game: motif data/ikemen1/system.def" tests\out\latest_game.log >nul || (echo FAIL: the game folder's mugen.cfg and system.def were not read & exit /b 1)
findstr /c:"select: stage Training Stage Z" tests\out\latest_game.log >nul || (echo FAIL: the game folder's stage list was not used & exit /b 1)
findstr /c:"Characters: 3 of 3 load" tests\out\compat_game.txt >nul || (echo FAIL: the compatibility check did not load the test game's characters & exit /b 1)
findstr /c:"3 pairs" tests\out\soak_game.txt >nul || (echo FAIL: the soak did not run every pair of the test game & exit /b 1)
findstr /c:"controller makerainbow" tests\out\soak_game.txt >nul || (echo FAIL: the soak did not note Odd KFM's unknown controller & exit /b 1)
findstr /c:"makerainbow" tests\out\compat_game.txt >nul || (echo FAIL: the compatibility check missed an unknown controller & exit /b 1)
findstr /c:"match: round 1" tests\out\latest_game.log >nul || (echo FAIL: the game folder's fight did not start & exit /b 1)
findstr /c:"options: p1 A = RB" tests\out\latest_keys.log >nul || (echo FAIL: the joystick config did not take RB for A & exit /b 1)
findstr /c:"Z = Not used" tests\out\keys_config.ini >nul || (echo FAIL: the button RB left did not become Not used & exit /b 1)
findstr /c:"title: OPTIONS" tests\out\latest_options.log >nul || (echo FAIL: OPTIONS did not open from the title & exit /b 1)
findstr /c:"options: difficulty 7" tests\out\latest_options.log >nul || (echo FAIL: the options screen did not raise Difficulty & exit /b 1)
findstr /c:"options: roundtime 98" tests\out\latest_options.log >nul || (echo FAIL: the options screen did not lower Time Limit & exit /b 1)
findstr /c:"settings: saved to tests/out/config.ini" tests\out\latest_options.log >nul || (echo FAIL: Save and Return did not save & exit /b 1)
findstr /c:"Difficulty = 7" tests\out\config.ini >nul || (echo FAIL: the saved file lacks the new Difficulty & exit /b 1)
findstr /c:"select: WATCH MODE" tests\out\latest_watch.log >nul || (echo FAIL: Watch did not open its select screen & exit /b 1)
findstr /c:"select: p2 Kung Fu Man" tests\out\latest_watch.log >nul || (echo FAIL: player 1 did not pick player 2 in Watch & exit /b 1)
findstr /r /c:"^p1 frame [1-9][0-9]*: state 0 -> [1-9]" tests\out\latest_watch.log >nul || (echo FAIL: the computer did not play player 1 in Watch & exit /b 1)
findstr /c:"simul: " tests\out\latest_simul.log >nul || (echo FAIL: Simul did not load four fighters & exit /b 1)
findstr /r /c:"^p2 frame .*: KO" tests\out\latest_simul.log >nul || (echo FAIL: Simul player 2 was not knocked out & exit /b 1)
findstr /r /c:"^p4 frame .*: KO" tests\out\latest_simul.log >nul || (echo FAIL: Simul player 4 was not knocked out & exit /b 1)
findstr /c:"round 1: KO, wins 1-0" tests\out\latest_simul.log >nul || (echo FAIL: Simul did not end the round when the whole team was KO & exit /b 1)
findstr /c:"state 1002 -> 1009" tests\out\latest_misc.log >nul || (echo FAIL: 7d controllers and triggers did not all check out & exit /b 1)
findstr /r /c:"^p1 frame .*state 1500 hits: .*on p2h50" tests\out\latest_assist.log >nul || (echo FAIL: the punch did not hit the assist helper & exit /b 1)
findstr /c:"p2h50 frame 21: state 9000 -> 5000" tests\out\latest_assist.log >nul || (echo FAIL: the assist helper did not go into its get-hit state & exit /b 1)
findstr /c:"state 1501 -> 1502" tests\out\latest_assist.log >nul || (echo FAIL: the hit helper was not a target for NumTarget and the Target redirect & exit /b 1)
findstr /c:"helper 88 state 1200" tests\out\latest_helper.log >nul || (echo FAIL: nested Helper was not created & exit /b 1)
findstr /c:"state 1000 -> 1002" tests\out\latest_helper.log >nul || (echo FAIL: Helper parent/root redirects or NumHelper failed & exit /b 1)
findstr /c:"helper 77 destroyed" tests\out\latest_helper.log >nul || (echo FAIL: DestroySelf did not remove the helper & exit /b 1)
findstr /c:"projectile 8 clashes with p2 projectile 8" tests\out\latest_projectile_clash.log >nul || (echo FAIL: Projectiles did not cancel each other & exit /b 1)
findstr /c:"p1 frame 24: state 0 -> 1002" tests\out\latest_projectile_clash.log >nul || (echo FAIL: ProjCancelTime did not see the cancelled projectile & exit /b 1)
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

:training
echo     training
"%EXE%" --mode training --input tests\inputs\hits.txt --quickstart --quit 180 --trace %SHOW% >nul || exit /b 1
copy /y m360.log tests\out\latest_training.log >nul
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

:assist
echo     assist helper
"%EXE%" --p1 tests/fixtures/targets/targets.def --p2 tests/fixtures/assist/assist.def --input tests\inputs\assist.txt --quickstart --quit 90 --trace %SHOW% >nul || exit /b 1
copy /y m360.log tests\out\latest_assist.log >nul
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

:arcade
echo     arcade
"%EXE%" --title --input tests\inputs\arcade.txt --quit 500 --trace %SHOW% >nul || exit /b 1
copy /y m360.log tests\out\latest_arcade.log >nul
exit /b 0

:game
echo     game folder
"%EXE%" --game tests\game --title --input tests\inputs\title.txt --quit 900 --trace %SHOW% >nul || exit /b 1
copy /y m360.log tests\out\latest_game.log >nul
"%EXE%" --check tests\game --quit 1 >nul || exit /b 1
copy /y compat_game.txt tests\out\compat_game.txt >nul
"%EXE%" --soak tests\game --soakframes 1800 --quit 1 >nul || exit /b 1
copy /y soak_game.txt tests\out\soak_game.txt >nul
exit /b 0

:keys
echo     keys
"%EXE%" --title --input tests\inputs\keys.txt --quit 600 --trace %SHOW% >nul || exit /b 1
copy /y m360.log tests\out\latest_keys.log >nul
copy /y tests\out\config.ini tests\out\keys_config.ini >nul
exit /b 0

:options
echo     options
"%EXE%" --title --input tests\inputs\options.txt --quit 520 --trace %SHOW% >nul || exit /b 1
copy /y m360.log tests\out\latest_options.log >nul
exit /b 0

:watch
echo     watch
"%EXE%" --select --mode watch --input tests\inputs\watch.txt --quickstart --quit 900 --trace %SHOW% >nul || exit /b 1
copy /y m360.log tests\out\latest_watch.log >nul
exit /b 0

:simul
echo     simul
"%EXE%" --select --mode simul --input tests\inputs\simul.txt --input2 tests\inputs\simul_p2.txt --quickstart --p2life 1 --quit 1700 --trace %SHOW% >nul || exit /b 1
copy /y m360.log tests\out\latest_simul.log >nul
exit /b 0

:survival
echo     survival
"%EXE%" --select --mode survival --noai --input tests\inputs\survival.txt --quickstart --p2life 1 --quit 1400 --trace %SHOW% >nul || exit /b 1
copy /y m360.log tests\out\latest_survival.log >nul
exit /b 0

:team
echo     team versus
"%EXE%" --title --input tests\inputs\team.txt --input2 tests\inputs\team_p2.txt --quickstart --p2life 1 --quit 1600 --trace %SHOW% >nul || exit /b 1
copy /y m360.log tests\out\latest_team.log >nul
exit /b 0

:misc
echo     misc controllers
"%EXE%" --p1 tests/fixtures/misc/misc.def --input tests/inputs/misc.txt --quickstart --quit 60 --trace %SHOW% >nul || exit /b 1
copy /y m360.log tests\out\latest_misc.log >nul
exit /b 0
