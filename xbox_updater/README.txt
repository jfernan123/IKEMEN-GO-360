MUGEN360 XBOX UPDATER
=====================

Puts the newest mugen360 build on your Xbox over your home network, no USB.

ONE-TIME SETUP
1. On the Xbox, in Aurora: Settings > Network (or General) > turn on the FTP
   server. Note the console's IP address and the FTP login (default
   xboxftp / xboxftp).
2. Open update_xbox.ps1 in Notepad and set the four lines at the top:
   ConsoleIp, FtpUser, FtpPass, and GameDir (the folder on the console where
   mugen360's default.xex is, e.g. Hdd1/Homebrew/mugen360).
3. Accept Juan's invite to the GitHub repo (it's private), using the GitHub
   account you'll log in with in the next step.

EACH UPDATE
- Have the Xbox on, in Aurora, on the same network as this PC.
- Double-click update_xbox.bat. The first time it installs the GitHub CLI and
  asks you to log in to GitHub.
- It grabs default.log from your last play session and shares it as a private
  link (copied to your clipboard: paste it to Juan), then downloads the
  newest build and uploads it to the console. Launch mugen360 from Aurora.

Keep Aurora's FTP on your home network only: don't port-forward it, the
default login is public knowledge and FTP isn't encrypted.
