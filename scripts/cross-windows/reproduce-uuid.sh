#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
source "$(dirname "$0")/env.sh"
build="$MP_WIN_WORK/build/windows/uuid-repro"
cmake -S "$MP_WIN_SCRIPTS/uuid-repro" -B "$build" -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$MP_WIN_SCRIPTS/toolchain.cmake" -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="$MP_WIN_QT" -DQT_HOST_PATH="$MP_WIN_WORK/toolchains/qt"
if cmake --build "$build" --parallel "$MP_WIN_JOBS" --target uuid-ambiguous > "$MP_WIN_WORK/logs/windows/uuid-ambiguous.log" 2>&1; then
  echo 'The expected Windows UUID ambiguity did not reproduce.' >&2
  exit 1
fi
rg -q "overloaded operator '=' is ambiguous" "$MP_WIN_WORK/logs/windows/uuid-ambiguous.log"
cmake --build "$build" --parallel "$MP_WIN_JOBS" --target uuid-explicit
cp "$MP_WIN_QT/bin/Qt6Core.dll" "$build/"
cp "$MP_WIN_WORK/toolchains/windows/redist/"*.dll "$build/"
"$MP_WIN_SCRIPTS/wine-headless.sh" "$build/uuid-explicit.exe"
echo 'Confirmed: id = {} fails to compile; id = QUuid() compiles and runs.'
