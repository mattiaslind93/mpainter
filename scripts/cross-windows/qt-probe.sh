#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
source "$(dirname "$0")/env.sh"
bash "$MP_WIN_SCRIPTS/init-wine.sh"
build="$MP_WIN_WORK/build/windows/qt-probe"
stage="$MP_WIN_WORK/build/windows/qt-smoke"
cmake -S "$MP_WIN_SCRIPTS/qt-probe" -B "$build" -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$MP_WIN_SCRIPTS/toolchain.cmake" -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="$MP_WIN_QT" -DQT_HOST_PATH="$MP_WIN_WORK/toolchains/qt"
cmake --build "$build" --parallel "$MP_WIN_JOBS"
mkdir -p "$stage/platforms"
cp "$build/qt-probe.exe" "$stage/"
for dll in Qt6Core Qt6Gui Qt6Widgets; do cp "$MP_WIN_QT/bin/$dll.dll" "$stage/"; done
cp "$MP_WIN_QT/plugins/platforms/qoffscreen.dll" "$stage/platforms/"
cp "$MP_WIN_WORK/toolchains/windows/redist/"*.dll "$stage/"
python3 "$MP_WIN_SCRIPTS/check-pe.py" "$stage" --output "$MP_WIN_WORK/logs/windows/qt-dll-closure.json"
"$MP_WIN_SCRIPTS/wine-headless.sh" "$stage/qt-probe.exe" -platform offscreen
