#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/env.sh"
exec 9>"$MP_WIN_WORK/build/windows/build.lock"
flock -n 9 || { echo 'Another Windows build is running.' >&2; exit 1; }
bash "$MP_WIN_SCRIPTS/build-deps.sh"
python3 "$MP_WIN_SCRIPTS/prepare-cross.py"
python3 "$MP_WIN_SCRIPTS/fix-library-case.py"
bash "$MP_WIN_SCRIPTS/configure.sh"
python3 "$MP_WIN_SCRIPTS/fix-library-case.py"
xvfb-run -a -s '-screen 0 1280x800x24 -nolisten tcp' env MP_WIN_BUILD_HEADLESS=1 \
  cmake --build "$MP_WIN_WORK/build/windows/native" --parallel 6 --target bts-host blender
cmake --install "$MP_WIN_WORK/build/windows/native" --config Release
