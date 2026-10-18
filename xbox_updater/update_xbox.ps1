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
$stateFile = Join-Path $PSScriptRoot 'installed_build.txt'   # the upload the console has (tag/asset id)
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
	$GameDir = (Read-Host 'Folder on the Xbox for mugen360 (press Enter for Usb0/Content/0000000000000000/mugen360)').Trim().Trim('/').Replace('\', '/')
	if (-not $GameDir) { $GameDir = 'Usb0/Content/0000000000000000/mugen360' }
	Set-Content -Path $settingsFile -Value "ConsoleIp=$ConsoleIp`r`nGameDir=$GameDir" -Encoding ascii
	Write-Host "Saved to $settingsFile" -ForegroundColor Green
}
$ftp = "ftp://${ConsoleIp}:$FtpPort/$GameDir"
$login = "${FtpUser}:${FtpPass}"

function Say($text, $color = 'Gray') { Write-Host "[$(Get-Date -Format 'HH:mm:ss')] $text" -ForegroundColor $color }

# The most recently uploaded Xbox package across the releases, or $null. publish_360.bat re-uploads it to the same
# release each time, so the upload (its asset id) is what tells builds apart, not the release's tag.
# GitHub allows 60 unauthenticated checks an hour.
function Get-NewestBuild {
	$releases = Invoke-RestMethod -Uri "https://api.github.com/repos/$Repo/releases?per_page=20" -Headers @{ 'User-Agent' = 'mugen360-updater' }
	$newest = $null
	foreach ($release in $releases) {
		if ($release.draft) { continue }
		foreach ($zip in $release.assets | Where-Object { $_.name -eq $Asset }) {
			if (-not $newest -or [datetime]$zip.updated_at -gt [datetime]$newest.zip.updated_at) {
				$newest = @{ release = $release; zip = $zip }
			}
		}
	}
	if (-not $newest) { return $null }
	$uploaded = ([datetime]$newest.zip.updated_at).ToString('yyyy-MM-dd HH:mm')
	return [pscustomobject]@{
		Id = "$($newest.release.tag_name)/$($newest.zip.id)"
		Name = "$($newest.release.tag_name), uploaded $uploaded"
		Url = $newest.zip.browser_download_url
		Size = $newest.zip.size
	}
}

# Aurora's FTP server doesn't answer EPSV (curl's first choice of passive mode), so every FTP call uses plain PASV.
$ftpOptions = @('--disable-epsv', '--ftp-pasv')

# $null when the console's FTP answers, otherwise curl's reason.
function Test-Console {
	$ErrorActionPreference = 'Continue'   # curl writing its reason to stderr isn't a script error here
	$reason = curl.exe -s -S @ftpOptions --connect-timeout 5 --max-time 20 --list-only --user $login "ftp://${ConsoleIp}:$FtpPort/" -o NUL 2>&1
	if ($LASTEXITCODE -eq 0) { return $null }
	return "$reason".Trim()
}

function Install-Build($build) {
	$work = Join-Path $env:TEMP 'mugen360_update'
	if (Test-Path $work) { Remove-Item -Recurse -Force $work }
	New-Item -ItemType Directory -Path $work | Out-Null

	# The last session's log, before the new build overwrites it.
	$log = Join-Path $work 'default.log'
	curl.exe -s @ftpOptions --connect-timeout 5 --user $login "$ftp/default.log" -o $log
	if ($LASTEXITCODE -eq 0 -and (Test-Path $log) -and (Get-Item $log).Length -gt 0) {
		New-Item -ItemType Directory -Force -Path $logsDir | Out-Null
		$saved = Join-Path $logsDir "default_$(Get-Date -Format 'yyyy-MM-dd_HHmm').log"
		Copy-Item $log $saved
		Say "Saved the last session's log: $saved (send it to Juan)" Green
	}

	Say "Downloading $($build.Name) ($([math]::Round($build.Size / 1MB, 1)) MB)" Cyan
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
		curl.exe -s -S @ftpOptions --connect-timeout 10 --ftp-create-dirs --user $login -T $file.FullName "$ftp/$relative"
		if ($LASTEXITCODE -ne 0) { Write-Progress -Activity 'Uploading to the Xbox' -Completed; throw "Upload failed at $relative." }
	}
	Write-Progress -Activity 'Uploading to the Xbox' -Completed
	Set-Content -Path $stateFile -Value $build.Id -Encoding ascii
	Remove-Item -Recurse -Force $work
	Say "Installed $($build.Name): $($files.Count) files. Launch mugen360 from Aurora." Green
}

# Waits $seconds, or less if a key is pressed: returns 'check', 'force' or 'timeout'.
function Wait-ForNextCheck($seconds) {
	$until = (Get-Date).AddSeconds($seconds)
	# Without a real console window (input redirected) there are no keys: just wait.
	try { $null = [Console]::KeyAvailable } catch { Start-Sleep -Seconds $seconds; return 'timeout' }
	while ((Get-Date) -lt $until) {
		while ([Console]::KeyAvailable) {
			switch ([Console]::ReadKey($true).Key) {
				'C' { return 'check' }
				'F' { return 'force' }
			}
		}
		Start-Sleep -Milliseconds 250
	}
	return 'timeout'
}

Say "Watching github.com/$Repo for new Xbox builds (every $CheckMinutes min). Console: ${ConsoleIp}:$FtpPort, folder $GameDir" Cyan
Say 'Leave this window open. Keys: C = check now, F = force reinstall the newest build. Close the window to stop.'
$lastNote = ''
$force = $false
while ($true) {
	try {
		$installed = if (Test-Path $stateFile) { (Get-Content $stateFile -Raw).Trim() } else { '' }
		$build = Get-NewestBuild
		if (-not $build) { $note = 'No Xbox build on GitHub yet.' }
		elseif ($build.Id -eq $installed -and -not $force) { $note = "Up to date ($($build.Name))." }
		elseif ($reason = Test-Console) { $note = "Build $($build.Name) is waiting: can't reach the Xbox at ${ConsoleIp}:$FtpPort ($reason). Is it on, in Aurora, FTP enabled, same network, right IP?" }
		else { Install-Build $build; $note = '' }
	} catch {
		$note = "Problem: $($_.Exception.Message) Trying again in $CheckMinutes min."
	}
	# Only print a status when it changes (or was asked for), so the window stays readable.
	if ($note -and ($note -ne $lastNote -or $asked)) { Say $note Yellow }
	$lastNote = $note
	$next = Wait-ForNextCheck ($CheckMinutes * 60)
	$asked = $next -ne 'timeout'
	$force = $next -eq 'force'
	if ($next -eq 'check') { Say 'Checking now...' Cyan }
	if ($next -eq 'force') { Say 'Forcing a reinstall of the newest build...' Cyan }
}
