#!/bin/bash
# Builds build/x360/mugen360.xex with xsdk in WSL Debian, then lays out dist/mugen360/ (default.xex + data/) to copy
# to a console or open in Xenia. Called by build_360.bat.
set -e
export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
cd "$(dirname "$0")"
XSDK="${XSDK:-$HOME/xsdk}"
if [ ! -f build/x360/build.ninja ]; then
	cmake -S . -B build/x360 -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE="$XSDK/cmake/xsdk.cmake"
fi
cmake --build build/x360
rm -rf dist/mugen360
mkdir -p dist/mugen360
cp build/x360/mugen360.xex dist/mugen360/default.xex
cp -r data dist/mugen360/data
# The options save to save/config.ini. xsdk can't make folders, so the package brings save/ (a README keeps it
# in the zip).
mkdir -p dist/mugen360/save
echo 'mugen360 keeps its options here (config.ini, written by the OPTIONS menu).' > dist/mugen360/save/README.txt
cp dist_README.txt dist/mugen360/README.txt
rm -f dist/mugen360_360.zip
(cd dist && python3 -c "import shutil; shutil.make_archive('mugen360_360', 'zip', '.', 'mugen360')")
echo "Built dist/mugen360/default.xex and dist/mugen360_360.zip"
