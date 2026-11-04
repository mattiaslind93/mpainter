#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/env.sh"
bash "$MP_WIN_SCRIPTS/init-wine.sh"
cmake -S "$MP_WIN_SCRIPTS/probe" -B "$MP_WIN_WORK/build/windows/probe" -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$MP_WIN_SCRIPTS/toolchain.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build "$MP_WIN_WORK/build/windows/probe" --parallel 6
llvm-readobj --file-headers --coff-imports "$MP_WIN_WORK/build/windows/probe/toolchain-probe.exe"
"$MP_WIN_SCRIPTS/wine-headless.sh" "$MP_WIN_WORK/build/windows/probe/toolchain-probe.exe"
