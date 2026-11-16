@echo off
rem Builds the PC version: build\pc\mugen360.exe (MSVC from VS 2022 Build Tools, SDL3 from ..\tools\sdl3).
rem Optimized with symbols (RelWithDebInfo): a Debug build runs the game about 10x slower. build_pc_test.bat stays a
rem Debug build, with MSVC's checked containers, for the tests.
setlocal
cd /d "%~dp0"
set "VSDIR=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools"
call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
set "PATH=%VSDIR%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;%PATH%"
cmake -S . -B build\pc -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo >nul || exit /b 1
cmake --build build\pc || exit /b 1
echo Built build\pc\mugen360.exe
