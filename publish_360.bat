@echo off
rem Builds the Xbox 360 package and posts it as a GitHub Release of this repo, where xbox_updater\update_xbox.bat
rem (on the PC next to the console) picks up the newest one. Needs the GitHub CLI, logged in (gh auth login).
setlocal
cd /d "%~dp0"
rem The stamp names the release (xbox-<stamp>) and is built into the game, which shows it during fights.
for /f %%d in ('powershell -NoProfile -Command "Get-Date -Format yyyy.MM.dd-HHmm"') do set "STAMP=%%d"
set "MUGEN360_VERSION=%STAMP%"
call build_360.bat || exit /b 1
for /f %%c in ('git rev-parse --short HEAD') do set "COMMIT=%%c"
for /f %%b in ('git branch --show-current') do set "BRANCH=%%b"
set "TAG=xbox-%STAMP%"
gh release create "%TAG%" dist\mugen360_360.zip --title "Xbox build %STAMP%" --notes "Built from %BRANCH% at %COMMIT%. On the PC next to the console, run xbox_updater\update_xbox.bat to install it." || exit /b 1
echo Published %TAG%: the console's PC can run update_xbox.bat now.
