@echo off
rem Builds the browser version with Emscripten: build\web\mugen360.js + mugen360.wasm, copied with web\index.html into
rem dist\web\. The game's files are packed separately by packaging\package_web.py (dist\web\game.data).
rem Emscripten lives at %USERPROFILE%\emsdk (or EMSDK_ROOT): a path without spaces, since under a path with spaces its
rem tools reach it by two names (long and 8.3) and each wipes the other's cache.
setlocal
cd /d "%~dp0"
if not defined EMSDK_ROOT set "EMSDK_ROOT=%USERPROFILE%\emsdk"
if not exist "%EMSDK_ROOT%\emsdk_env.bat" (
  echo Emscripten isn't installed at %EMSDK_ROOT%: git clone https://github.com/emscripten-core/emsdk there, then
  echo emsdk install latest and emsdk activate latest.
  exit /b 1
)
call "%EMSDK_ROOT%\emsdk_env.bat" >nul 2>nul
rem CMake and Ninja from the VS Build Tools, as build_pc.bat uses.
set "VSDIR=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools"
set "PATH=%VSDIR%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;%VSDIR%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;%PATH%"
rem SDL3 for the browser, fetched and built once (in parallel, every compile would race to fetch it).
call embuilder build sdl3 >nul || exit /b 1
if not exist build\web\build.ninja (
  call emcmake cmake -S . -B build\web -G Ninja -DCMAKE_BUILD_TYPE=Release || exit /b 1
)
cmake --build build\web || exit /b 1
if not exist dist\web mkdir dist\web
copy /y build\web\mugen360.js dist\web\ >nul || exit /b 1
copy /y build\web\mugen360.wasm dist\web\ >nul || exit /b 1
copy /y web\index.html dist\web\ >nul || exit /b 1
echo Built dist\web (engine). Pack the game with packaging\package_web.py.
