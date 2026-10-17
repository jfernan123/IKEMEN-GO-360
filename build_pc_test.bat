@echo off
rem Builds a second PC copy, build\pc_test\mugen360.exe, so scripted tests can run while build\pc\mugen360.exe is open
rem (Windows locks a running .exe, and build_pc.bat then fails to link).
setlocal
cd /d "%~dp0"
set "VSDIR=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools"
call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
set "PATH=%VSDIR%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;%PATH%"
if not exist build\pc_test\build.ninja cmake -S . -B build\pc_test -G Ninja -DCMAKE_BUILD_TYPE=Debug || exit /b 1
cmake --build build\pc_test || exit /b 1
echo Built build\pc_test\mugen360.exe
