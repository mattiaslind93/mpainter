#!/usr/bin/env bash
set -euo pipefail
# Run inside bts-build; execute sequentially with other builds and graphics tests.
ulimit -c 0
root="${BTS_ROOT:-/mnt/mpainter-phase1}"
out="${1:-$root/build/crashcurv/rebake}"
mkdir -p "$out"
unset WAYLAND_DISPLAY
export QT_QPA_PLATFORM=xcb QT_PLUGIN_PATH="$root/toolchains/qt/plugins"
export LD_LIBRARY_PATH="$root/toolchains/qt/lib:$root/build/runtime/lib"
export OMP_NUM_THREADS=6 OPENBLAS_NUM_THREADS=1
export XDG_CONFIG_HOME="$out/config" XDG_CACHE_HOME="$out/cache" XDG_DATA_HOME="$out/data" BTS_RECOVERY_DIR="$out/recovery"
export BTS_REBAKE_REGRESSION=1
runner=()
if [[ ${BTS_REBAKE_GDB:-0} == 1 ]]; then
  runner=(gdb -batch -ex 'set pagination off' -ex 'set print thread-events off' -ex run -ex 'bt 16' -ex 'info registers rax rdx' -ex 'x/8i $pc-16' --args)
fi
xvfb-run -a "${runner[@]}" "${BTS_HOST_EXECUTABLE:-$root/build/linux-native/bin/mpainter}" --no-splash --cpu-bake-source-test "$out"
