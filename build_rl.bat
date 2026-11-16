@echo off
rem Builds an optimized PC copy for RL training, build\rl\mugen360.exe. The other PC builds are Debug builds (no
rem optimization, MSVC's checked containers), which run the fight simulation many times slower. RelWithDebInfo keeps
rem symbols so a profiler can still name functions.
setlocal
cd /d "%~dp0"
set "VSDIR=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools"
call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
set "PATH=%VSDIR%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;%PATH%"
if not exist build\rl\build.ninja cmake -S . -B build\rl -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo || exit /b 1
cmake --build build\rl --target mugen360 || exit /b 1
echo Built build\rl\mugen360.exe
