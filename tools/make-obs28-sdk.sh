#!/usr/bin/env bash
# Builds the OBS 28.0.3 + Qt 6.3.1 SDK used to compile the plugin for OBS 28 and later
# (work/obs28-sdk), then configures work/build-obs28. No libobs build: headers come from
# the 28.0.3 source, import libraries are generated from the official 28.0.3 DLLs.
#
# Needs: gh (GitHub CLI), unzip, python, MSVC Build Tools 18 (dumpbin, lib, cmake), and
# the official OBS 28.0.3 portable zip extracted at work/obs-compat/obs-28.0.3 (for its DLLs).
# Run from the project root:  bash tools/make-obs28-sdk.sh
set -euo pipefail

ROOT=$(pwd)
W=$ROOT/work
SDK=$W/obs28-sdk
VS="/c/Program Files (x86)/Microsoft Visual Studio/18/BuildTools"
MSVC=$(ls -d "$VS/VC/Tools/MSVC/"*/bin/Hostx64/x64 | tail -1)
export PATH="$PATH:$VS/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin"

mkdir -p "$SDK/dl"
cd "$SDK/dl"
[ -f windows-deps-qt6-2022-08-02-x64.zip ] || gh release download 2022-08-02 -R obsproject/obs-deps -p windows-deps-qt6-2022-08-02-x64.zip
[ -f obs-studio-28.0.3-src.zip ] || gh api repos/obsproject/obs-studio/zipball/28.0.3 > obs-studio-28.0.3-src.zip
sha256sum -c - <<'EOF'
e5509b54196a3f935250cc4b9c54160c8e588fd0f92bc078a2a64f9d9e2e4e93  windows-deps-qt6-2022-08-02-x64.zip
216e28b6233f3e85e94b9920da5d103a32c9e7d80dcbdd1776b5dd5babc6e828  obs-studio-28.0.3-src.zip
EOF

cd "$SDK"
rm -rf src qt6 sdk
mkdir -p src qt6 sdk/include sdk/lib sdk/bin/64bit sdk/cmake
unzip -q dl/obs-studio-28.0.3-src.zip -d src
unzip -q dl/windows-deps-qt6-2022-08-02-x64.zip -d qt6
S=$(ls -d src/*/)

# Headers: libobs tree, frontend API, w32-pthreads, and a hand-written obsconfig.h
(cd "$S/libobs" && find . \( -name '*.h' -o -name '*.hpp' \) | while read -r f; do
    mkdir -p "$SDK/sdk/include/$(dirname "$f")"; cp "$f" "$SDK/sdk/include/$f"; done)
cp "$S/UI/obs-frontend-api/obs-frontend-api.h" sdk/include/
cp "$S/deps/w32-pthreads/pthread.h" "$S/deps/w32-pthreads/sched.h" sdk/include/
cat > sdk/include/obsconfig.h <<'EOF'
#pragma once

/* Hand-written for building plugins against OBS 28.0.3 headers (no libobs build). */
#ifndef ON
#define ON 1
#endif
#ifndef OFF
#define OFF 0
#endif

#define OBS_VERSION "28.0.3"
#define OBS_VERSION_CANONICAL "28.0.3"
#define OBS_DATA_PATH "../../data"
#define OBS_INSTALL_PREFIX ""
#define OBS_PLUGIN_DESTINATION "obs-plugins/64bit"
#define OBS_QT_VERSION 6

#define OBS_RELEASE_CANDIDATE 0
#define OBS_BETA 0
EOF

# Import libraries from the official 28.0.3 DLLs
cp "$W/obs-compat/obs-28.0.3/bin/64bit/"{obs.dll,obs-frontend-api.dll,w32-pthreads.dll} sdk/bin/64bit/
for n in obs obs-frontend-api w32-pthreads; do
    "$MSVC/dumpbin.exe" //EXPORTS "sdk/bin/64bit/$n.dll" | python -c "
import re, sys
names = [m.group(1) for l in sys.stdin for m in [re.match(r'\s+\d+\s+[0-9A-F]+\s+[0-9A-F]{8}\s+(\S+)', l)] if m]
print('LIBRARY $n.dll'); print('EXPORTS'); print('\n'.join(names))" > "sdk/lib/$n.def"
    MSYS_NO_PATHCONV=1 "$MSVC/lib.exe" /nologo /def:sdk/lib/$n.def /machine:x64 /out:sdk/lib/$n.lib > /dev/null
done
rm -f sdk/lib/*.exp

# CMake package files: same layout as the template's .deps install, Release only
cp "$ROOT/upstream/obs-profanity-filter/.deps/cmake/"*.cmake sdk/cmake/
rm -f sdk/cmake/*-debug.cmake
sed -i 's/set(PACKAGE_VERSION "[^"]*")/set(PACKAGE_VERSION "28.0.3")/' sdk/cmake/*ConfigVersion.cmake

# Configure (Qt tool packages pinned too, otherwise the template's newer Qt is picked up)
cd "$ROOT"
Q=$SDK/qt6/lib/cmake
cmake -S upstream/obs-profanity-filter -B work/build-obs28 -G "Visual Studio 18 2026" -A x64 \
    -DCMAKE_PREFIX_PATH="$SDK/qt6" -DQt6_DIR="$Q/Qt6" -DQt6CoreTools_DIR="$Q/Qt6CoreTools" \
    -DQt6GuiTools_DIR="$Q/Qt6GuiTools" -DQt6WidgetsTools_DIR="$Q/Qt6WidgetsTools" \
    -Dlibobs_DIR="$SDK/sdk/cmake" -Dobs-frontend-api_DIR="$SDK/sdk/cmake" -Dw32-pthreads_DIR="$SDK/sdk/cmake" \
    -DFETCHCONTENT_SOURCE_DIR_SHERPA_ONNX="$W/build-padding/_deps/sherpa_onnx-src" \
    -DFETCHCONTENT_SOURCE_DIR_ZLIB="$W/build-padding/_deps/zlib-src" \
    -DFETCHCONTENT_SOURCE_DIR_CURL="$W/curl-local/curl-8.11.0"
echo "Build: cmake --build work/build-obs28 --config Release --target obs-profanity-filter --parallel 6"
