# Keeps the Xbox on the newest mugen360 build. Leave this window open: every few minutes it checks GitHub, and
# when there's a build the console doesn't have yet (and the console is on, in Aurora), it
#   1. saves default.log from the last play session next to this script (send it to Juan),
#   2. downloads the new build from the repo's GitHub Releases (public: no account needed),
#   3. uploads it to the console with Aurora's FTP server.
# Run it with update_xbox.bat. The first run asks for the console's IP and game folder and saves them in
# settings.txt next to this script (delete that file to be asked again).

$FtpPort   = 21                           # Aurora's FTP server
$FtpUser   = 'xboxftp'
$FtpPass   = 'xboxftp'
$CheckMinutes = 3                         # how often to look for a new build

$Repo = 'jfernan123/IKEMEN-GO-360'
$Asset = 'mugen360_360.zip'
$ErrorActionPreference = 'Stop'
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

$settingsFile = Join-Path $PSScriptRoot 'settings.txt'
$stateFile = Join-Path $PSScriptRoot 'installed_build.txt'   # the release tag the console has
$logsDir = Join-Path $PSScriptRoot 'logs'

# The console's address and game folder: asked once, then read from settings.txt.
if (Test-Path $settingsFile) {
	$settings = Get-Content $settingsFile | ConvertFrom-StringData
	$ConsoleIp = $settings.ConsoleIp
	$GameDir = $settings.GameDir
} else {
	Write-Host 'First run: tell me where the Xbox is.' -ForegroundColor Cyan
	do { $ConsoleIp = (Read-Host "The Xbox's IP address (Aurora: Settings > Network, e.g. 192.168.1.50)").Trim() }
	until ($ConsoleIp -match '^\d{1,3}(\.\d{1,3}){3}$')
	$GameDir = (Read-Host 'Folder on the Xbox for mugen360 (press Enter for Hdd1/mugen360)').Trim().Trim('/').Replace('\', '/')
	if (-not $GameDir) { $GameDir = 'Hdd1/mugen360' }
	Set-Content -Path $settingsFile -Value "ConsoleIp=$ConsoleIp`r`nGameDir=$GameDir" -Encoding ascii
	Write-Host "Saved to $settingsFile" -ForegroundColor Green
}
$ftp = "ftp://${ConsoleIp}:$FtpPort/$GameDir"
$login = "${FtpUser}:${FtpPass}"

function Say($text, $color = 'Gray') { Write-Host "[$(Get-Date -Format 'HH:mm:ss')] $text" -ForegroundColor $color }

# The newest release that has the Xbox package, or $null. GitHub allows 60 unauthenticated checks an hour.
function Get-NewestBuild {
	$releases = Invoke-RestMethod -Uri "https://api.github.com/repos/$Repo/releases?per_page=10" -Headers @{ 'User-Agent' = 'mugen360-updater' }
	foreach ($release in $releases) {
		if ($release.draft) { continue }
		$zip = $release.assets | Where-Object { $_.name -eq $Asset } | Select-Object -First 1
		if ($zip) { return [pscustomobject]@{ Tag = $release.tag_name; Url = $zip.browser_download_url; Size = $zip.size } }
	}
	return $null
}

function Test-Console {
	curl.exe -s --connect-timeout 5 --max-time 15 --list-only --user $login "ftp://${ConsoleIp}:$FtpPort/" -o NUL
	return $LASTEXITCODE -eq 0
}

function Install-Build($build) {
	$work = Join-Path $env:TEMP 'mugen360_update'
	if (Test-Path $work) { Remove-Item -Recurse -Force $work }
	New-Item -ItemType Directory -Path $work | Out-Null

	# The last session's log, before the new build overwrites it.
	$log = Join-Path $work 'default.log'
	curl.exe -s --connect-timeout 5 --user $login "$ftp/default.log" -o $log
	if ($LASTEXITCODE -eq 0 -and (Test-Path $log) -and (Get-Item $log).Length -gt 0) {
		New-Item -ItemType Directory -Force -Path $logsDir | Out-Null
		$saved = Join-Path $logsDir "default_$(Get-Date -Format 'yyyy-MM-dd_HHmm').log"
		Copy-Item $log $saved
		Say "Saved the last session's log: $saved (send it to Juan)" Green
	}

	Say "Downloading $($build.Tag) ($([math]::Round($build.Size / 1MB, 1)) MB)" Cyan
	$zip = Join-Path $work $Asset
	curl.exe -s -S -L --fail -o $zip $build.Url
	if ($LASTEXITCODE -ne 0) { throw 'Download failed.' }
	Expand-Archive -Path $zip -DestinationPath $work -Force
	$root = Join-Path $work 'mugen360'

	Say "Uploading to the console: $GameDir" Cyan
	$files = Get-ChildItem -Path $root -Recurse -File
	$i = 0
	foreach ($file in $files) {
		$i++
		$relative = $file.FullName.Substring($root.Length + 1).Replace('\', '/')
		Write-Progress -Activity 'Uploading to the Xbox' -Status $relative -PercentComplete ($i * 100 / $files.Count)
		curl.exe -s -S --connect-timeout 10 --ftp-create-dirs --user $login -T $file.FullName "$ftp/$relative"
		if ($LASTEXITCODE -ne 0) { Write-Progress -Activity 'Uploading to the Xbox' -Completed; throw "Upload failed at $relative." }
	}
	Write-Progress -Activity 'Uploading to the Xbox' -Completed
	Set-Content -Path $stateFile -Value $build.Tag -Encoding ascii
	Remove-Item -Recurse -Force $work
	Say "Installed $($build.Tag): $($files.Count) files. Launch mugen360 from Aurora." Green
}

Say "Watching github.com/$Repo for new Xbox builds (every $CheckMinutes min). Console: ${ConsoleIp}:$FtpPort, folder $GameDir" Cyan
Say 'Leave this window open. Close it to stop.'
$lastNote = ''
while ($true) {
	try {
		$installed = if (Test-Path $stateFile) { (Get-Content $stateFile -Raw).Trim() } else { '' }
		$build = Get-NewestBuild
		if (-not $build) { $note = 'No Xbox build on GitHub yet.' }
		elseif ($build.Tag -eq $installed) { $note = "Up to date ($installed)." }
		elseif (-not (Test-Console)) { $note = "New build $($build.Tag) is waiting: turn the Xbox on, open Aurora, check FTP is enabled." }
		else { Install-Build $build; $note = '' }
	} catch {
		$note = "Problem: $($_.Exception.Message) Trying again in $CheckMinutes min."
	}
	# Only print a status when it changes, so the window stays readable.
	if ($note -and $note -ne $lastNote) { Say $note Yellow }
	$lastNote = $note
	Start-Sleep -Seconds ($CheckMinutes * 60)
}
