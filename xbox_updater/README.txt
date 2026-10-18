MUGEN360 XBOX UPDATER
=====================

Keeps the Xbox on the newest mugen360 build over your home network, no USB,
no GitHub account. Runs on this PC: unzip this folder anywhere and keep it.

ONE-TIME SETUP
1. On the Xbox, in Aurora: Settings > FTP Server > tick Enabled
   (port 21, login xboxftp / xboxftp: the updater already uses those).
2. Double-click update_xbox.bat. The first time it asks for:
   - the Xbox's IP address (Aurora shows it in Settings > Network),
   - the folder on the Xbox for mugen360 (just press Enter for
     Usb0/Content/0000000000000000/mugen360).
   It remembers them in settings.txt (delete that file to change them).
3. In Aurora, add Usb0/Content/0000000000000000/mugen360 as a scan path
   once so mugen360 shows up.

USING IT
- Double-click update_xbox.bat and leave the window open.
- Every few minutes it checks GitHub. When Juan posts a new build and the
  Xbox is on (in Aurora), it downloads it and copies it to the console.
  If the Xbox is off, it waits and does it once the console is back.
- Before each install it saves default.log from your last play session in
  the logs folder here. Send the newest one to Juan.
- Keys in the window: C checks GitHub right now instead of waiting,
  F reinstalls the newest build even if the Xbox already has it.

Keep Aurora's FTP on your home network only: don't port-forward it. FTP
isn't encrypted and the default login is public knowledge.
