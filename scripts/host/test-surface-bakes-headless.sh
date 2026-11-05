#!/usr/bin/env bash
set -euo pipefail
# Run inside bts-build; keep this sequential with other GPU tests and packaging.
root="${BTS_ROOT:-/mnt/mpainter-phase1}"
out="${1:-$root/build/curvature/after}"
unset WAYLAND_DISPLAY
export QT_QPA_PLATFORM=xcb QT_PLUGIN_PATH="$root/toolchains/qt/plugins"
export LD_LIBRARY_PATH="$root/toolchains/qt/lib:$root/build/runtime/lib"
export OMP_NUM_THREADS=6 OPENBLAS_NUM_THREADS=1
export XDG_CONFIG_HOME="$root/build/mpainter-test-sessions/curvature/config"
export XDG_CACHE_HOME="$root/build/curvature/state/cache"
export XDG_DATA_HOME="$root/build/curvature/state/data"
export BTS_RECOVERY_DIR="$root/build/mpainter-test-sessions/curvature/recovery"
export BTS_SURFACE_FIXTURES="${BTS_SURFACE_FIXTURES:-$root/build/curvature/fixtures}"
option=--cpu-bake-source-test
if [[ ${BTS_SURFACE_GPU:-0} == 1 ]]; then
  option=--headless-surface-bake-test
  export BTS_ROOT="$root"
  source "$root/mpainter/scripts/host/gpu-env.sh"
fi
xvfb-run -a bash -c 'export BTS_PRIVATE_XVFB_PID=$(cat "/tmp/.X${DISPLAY#:}-lock"); exec "$@"' surface-proof "${BTS_HOST_EXECUTABLE:-$root/build/linux-native/bin/mpainter}" --no-splash "$option" "$out"
