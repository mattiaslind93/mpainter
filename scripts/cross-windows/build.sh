#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/env.sh"
exec 9>"$MP_WIN_WORK/build/windows/build.lock"
flock -n 9 || { echo 'Another Windows build is running.' >&2; exit 1; }
python3 "$MP_WIN_SCRIPTS/../host/check-windows-uuid-resets.py" "$MP_WIN_WORK/build/windows/mpainter-source/native"
bash "$MP_WIN_SCRIPTS/build-deps.sh"
python3 "$MP_WIN_SCRIPTS/prepare-cross.py"
python3 "$MP_WIN_SCRIPTS/fix-library-case.py"
bash "$MP_WIN_SCRIPTS/configure.sh"
python3 "$MP_WIN_SCRIPTS/fix-library-case.py"
xvfb-run -a -s '-screen 0 1280x800x24 -nolisten tcp' env MP_WIN_BUILD_HEADLESS=1 \
  cmake --build "$MP_WIN_WORK/build/windows/native" --parallel "$MP_WIN_JOBS" --target bts-host blender blender-launcher bf_intern_draco_bridge bf_intern_meshopt_bridge cycles_osl_shaders
cmake --install "$MP_WIN_WORK/build/windows/native" --config Release
