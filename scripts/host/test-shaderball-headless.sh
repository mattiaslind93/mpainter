#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
# Run with distrobox enter bts-build. One bounded GUI/GPU process at a time.
test -f /run/.containerenv
ulimit -c 0
root=/mnt/mpainter-phase1
out="${BTS_SHADERBALL_OUTPUT:-$root/build/shaderball-proof}"
mkdir -p "$out" "$root/build/tmp"
export TMPDIR="$root/build/tmp"
unset WAYLAND_DISPLAY
export QT_QPA_PLATFORM=xcb QT_PLUGIN_PATH="$root/toolchains/qt/plugins"
export LD_LIBRARY_PATH="$root/toolchains/qt/lib:$root/build/runtime/lib"
export OMP_NUM_THREADS=4 OPENBLAS_NUM_THREADS=1
export BTS_ROOT="$root" BTS_HOSTED_SHADER_WORKERS=0 BTS_SHADERBALL_DEMO=1 BTS_SKEW_DEMO=1
export BTS_HOST_EXECUTABLE="${BTS_HOST_EXECUTABLE:-$root/build/linux-native/bin/mpainter}"
export XDG_CONFIG_HOME="$out/mpainter-test-sessions/config" XDG_DATA_HOME="$out/data" BTS_RECOVERY_DIR="$out/recovery"
export BTS_ENVIRONMENT="$root/build/runtime/5.2/datafiles/studiolights/world/default-studio.hdr"
source "$root/mpainter/scripts/host/gpu-env.sh"
cd /tmp
xvfb-run -a -s '-screen 0 1600x1000x24' timeout 300 bash -c 'export BTS_PRIVATE_XVFB_PID=$(cat "/tmp/.X${DISPLAY#:}-lock"); exec "$BTS_HOST_EXECUTABLE" --workspace --gui-fix-demo "$1" --gui-fix-resolution 128' shaderball "$out"
