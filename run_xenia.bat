@echo off
rem Opens dist\mugen360\default.xex in Xenia Canary with the settings xsdk titles need. Build it with build_360.bat.
start "" "%~dp0..\tools\xenia-canary\xenia_canary.exe" --readback_resolve=full --async_shader_compilation=false --keyboard_mode=1 "%~dp0dist\mugen360\default.xex"
