@echo off
rem Builds the Xbox 360 package and posts it as a GitHub Release of this repo, where xbox_updater\update_xbox.bat
rem (on the PC next to the console) picks up the newest one. Needs the GitHub CLI, logged in (gh auth login).
setlocal
cd /d "%~dp0"
rem The stamp names the release (xbox-<stamp>) and is built into the game, which shows it during fights.
for /f %%d in ('powershell -NoProfile -Command "Get-Date -Format yyyy.MM.dd-HHmm"') do set "STAMP=%%d"
set "MUGEN360_VERSION=%STAMP%"
call "%~dp0build_360.bat" || exit /b 1
for /f %%c in ('git rev-parse --short HEAD') do set "COMMIT=%%c"
for /f %%b in ('git branch --show-current') do set "BRANCH=%%b"
set "TAG=xbox-%STAMP%"
rem The updater rides along so the console's PC can grab it from the same page.
powershell -NoProfile -Command "Compress-Archive -Path xbox_updater\update_xbox.bat, xbox_updater\update_xbox.ps1, xbox_updater\README.txt -DestinationPath dist\mugen360_xbox_updater.zip -Force" || exit /b 1
gh release create "%TAG%" dist\mugen360_360.zip dist\mugen360_xbox_updater.zip --target "%COMMIT%" --title "Xbox build %STAMP%" --notes "Built from %BRANCH% at %COMMIT%. On the PC next to the console, leave update_xbox.bat (in mugen360_xbox_updater.zip) running: it installs each new build over Aurora's FTP. Or copy mugen360_360.zip's mugen360 folder to the console by hand." || exit /b 1
echo Published %TAG%: the console's PC can run update_xbox.bat now.
