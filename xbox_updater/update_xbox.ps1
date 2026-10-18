# Installs the newest mugen360 Xbox build onto the console over the home network:
#   1. pulls default.log from the last play session off the console and shares it as a private GitHub gist
#      (the link is copied to the clipboard, ready to paste to Juan),
#   2. downloads the newest release from GitHub,
#   3. uploads it to the console with Aurora's FTP server.
# Run it with update_xbox.bat. Edit the four settings below once.

$ConsoleIp = '192.168.1.50'               # the console's address (Aurora shows it under Settings > Network)
$FtpUser   = 'xboxftp'                    # Aurora's FTP login
$FtpPass   = 'xboxftp'
$GameDir   = 'Hdd1/Homebrew/mugen360'     # where default.xex is on the console

$Repo = 'jfernan123/IKEMEN-GO-360'
$ErrorActionPreference = 'Stop'

function Step($text) { Write-Host "`n== $text" -ForegroundColor Cyan }

# The GitHub CLI, installed and logged in the first time.
if (-not (Get-Command gh -ErrorAction SilentlyContinue)) {
	Step 'Installing the GitHub CLI (first time only)'
	winget install --id GitHub.cli -e --accept-source-agreements --accept-package-agreements
	$env:Path = [Environment]::GetEnvironmentVariable('Path', 'Machine') + ';' + [Environment]::GetEnvironmentVariable('Path', 'User')
}
gh auth status 2>$null | Out-Null
if ($LASTEXITCODE -ne 0) {
	Step 'Log in to GitHub (first time only)'
	gh auth login
}

$work = Join-Path $env:TEMP 'mugen360_update'
if (Test-Path $work) { Remove-Item -Recurse -Force $work }
New-Item -ItemType Directory -Path $work | Out-Null
$ftp = "ftp://$ConsoleIp/$GameDir"
$login = "${FtpUser}:${FtpPass}"

Step "Getting default.log from the console ($ConsoleIp)"
$log = Join-Path $work 'default.log'
curl.exe -s -S --user $login "$ftp/default.log" -o $log
if ($LASTEXITCODE -eq 0 -and (Test-Path $log) -and (Get-Item $log).Length -gt 0) {
	$stamp = Get-Date -Format 'yyyy-MM-dd HH:mm'
	$url = gh gist create $log --desc "mugen360 default.log $stamp"
	Set-Clipboard -Value $url
	Write-Host "Log shared: $url (copied, paste it to Juan)" -ForegroundColor Green
} else {
	Write-Host 'No log on the console yet (or it could not be reached): skipping.' -ForegroundColor Yellow
}

Step 'Downloading the newest build'
gh release download --repo $Repo --pattern 'mugen360_360.zip' --dir $work --clobber
Expand-Archive -Path (Join-Path $work 'mugen360_360.zip') -DestinationPath $work -Force
$root = Join-Path $work 'mugen360'

Step "Uploading to the console: $GameDir"
$files = Get-ChildItem -Path $root -Recurse -File
$i = 0
foreach ($file in $files) {
	$i++
	$relative = $file.FullName.Substring($root.Length + 1).Replace('\', '/')
	Write-Progress -Activity 'Uploading' -Status $relative -PercentComplete ($i * 100 / $files.Count)
	curl.exe -s -S --ftp-create-dirs --user $login -T $file.FullName "$ftp/$relative"
	if ($LASTEXITCODE -ne 0) { throw "Upload failed at ${relative}: is the console on, in Aurora, with FTP enabled?" }
}
Write-Progress -Activity 'Uploading' -Completed
Write-Host "`nDone: $($files.Count) files. Launch mugen360 from Aurora." -ForegroundColor Green

