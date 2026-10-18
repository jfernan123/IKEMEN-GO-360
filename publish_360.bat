@echo off
rem Builds the Xbox 360 package and uploads it to the one GitHub release the console's PC watches, replacing the
rem previous build there (xbox_updater\update_xbox.bat notices the new upload and installs it). Creates the release
rem the first time. Needs the GitHub CLI, logged in (gh auth login).
setlocal
cd /d "%~dp0"
set "TAG=xbox-2026.10.18-usb"
rem The stamp is built into the game, which shows it during fights, and goes in the release's title.
for /f %%d in ('powershell -NoProfile -Command "Get-Date -Format yyyy.MM.dd-HHmm"') do set "STAMP=%%d"
set "MUGEN360_VERSION=%STAMP%"
call build_360.bat || exit /b 1
for /f %%c in ('git rev-parse --short HEAD') do set "COMMIT=%%c"
for /f %%b in ('git branch --show-current') do set "BRANCH=%%b"
rem The updater rides along so the console's PC can grab it from the same page.
powershell -NoProfile -Command "Compress-Archive -Path xbox_updater\update_xbox.bat, xbox_updater\update_xbox.ps1, xbox_updater\README.txt -DestinationPath dist\mugen360_xbox_updater.zip -Force" || exit /b 1
set "TITLE=Xbox build V%STAMP%"
set "NOTES=Newest build: V%STAMP% (from %BRANCH% at %COMMIT%), shown in the bottom-right corner during fights. On the PC next to the console, leave update_xbox.bat (in mugen360_xbox_updater.zip) running: it installs each new build over Aurora's FTP. Or copy mugen360_360.zip's mugen360 folder to the console by hand."
gh release view "%TAG%" >nul 2>&1
if errorlevel 1 (
	gh release create "%TAG%" dist\mugen360_360.zip dist\mugen360_xbox_updater.zip --title "%TITLE%" --notes "%NOTES%" || exit /b 1
) else (
	gh release upload "%TAG%" dist\mugen360_360.zip dist\mugen360_xbox_updater.zip --clobber || exit /b 1
	gh release edit "%TAG%" --title "%TITLE%" --notes "%NOTES%" --latest || exit /b 1
)
echo Uploaded V%STAMP% to %TAG%: the updater installs it within a few minutes.
