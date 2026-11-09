#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
# Run inside bts-build. One private X server, one small GPU workload at a time.
root=/mnt/mpainter-phase1
out="$root/build/checkbox-proof"
mkdir -p "$out" "$root/shots"
unset WAYLAND_DISPLAY
export QT_QPA_PLATFORM=xcb QT_PLUGIN_PATH="$root/toolchains/qt/plugins"
export LD_LIBRARY_PATH="$root/toolchains/qt/lib:$root/build/runtime/lib"
export OMP_NUM_THREADS=4 OPENBLAS_NUM_THREADS=1
xvfb-run -a -s '-screen 0 1600x1000x24' env BTS_CHECKBOX_SCREENSHOT="$root/shots/checkbox.png" "$root/build/linux-native/bin/bts-theme-tests"
"$root/build/linux-native/bin/bts-environment-rotation-gesture-tests"
"$root/build/linux-native/bin/bts-texture-set-residency-tests"
export BTS_ROOT="$root" BTS_HOST_EXECUTABLE="${BTS_HOST_EXECUTABLE:-$root/build/linux-native/bin/mpainter}"
export BTS_CHECKBOX_DEMO=1 BTS_HOSTED_SHADER_WORKERS=0 BTS_PERFORMANCE_PROFILE=1
export XDG_CONFIG_HOME="$root/build/mpainter-test-sessions/checkbox/config" XDG_DATA_HOME="$out/data" BTS_RECOVERY_DIR="$root/build/mpainter-test-sessions/checkbox/recovery"
source "$root/mpainter/scripts/host/gpu-env.sh"
export BTS_ENVIRONMENT="$root/build/runtime/5.2/datafiles/studiolights/world/default-studio.hdr"
xvfb-run -a -s '-screen 0 1600x1000x24' bash -c 'export BTS_PRIVATE_XVFB_PID=$(cat "/tmp/.X${DISPLAY#:}-lock"); exec "$BTS_HOST_EXECUTABLE" "$@"' checkbox-test --workspace --gui-fix-demo "$out" --gui-fix-mesh "$root/mpainter/docs/checkbox/residency.obj" --gui-fix-resolution 128
cp "$out/texturesets-tab.png" "$root/shots/texturesets-tab.png"
cp "$out/checkbox-regression.json" "$root/mpainter/docs/checkbox/validation.json"
