#!/usr/bin/env bash
set -euo pipefail
ulimit -c 0
# Inside bts-build only. Small fixtures, one process/GPU workload at a time.
root=/mnt/mpainter-phase1
out="$root/build/unloadfix-proof"
mkdir -p "$out" "$root/mpainter/docs/unloadfix"
unset WAYLAND_DISPLAY
export QT_QPA_PLATFORM=xcb QT_PLUGIN_PATH="$root/toolchains/qt/plugins"
export LD_LIBRARY_PATH="$root/toolchains/qt/lib:$root/build/runtime/lib"
export OMP_NUM_THREADS=4 OPENBLAS_NUM_THREADS=1
export BTS_ROOT="$root" BTS_HOSTED_SHADER_WORKERS=0 BTS_PERFORMANCE_PROFILE=1
export BTS_HOST_EXECUTABLE="${BTS_HOST_EXECUTABLE:-$root/build/linux-native/bin/mpainter}"
export XDG_CONFIG_HOME="$root/build/mpainter-test-sessions/unloadfix/config" XDG_DATA_HOME="$out/data" BTS_RECOVERY_DIR="$out/recovery"
export BTS_ENVIRONMENT="$root/build/runtime/5.2/datafiles/studiolights/world/default-studio.hdr"
source "$root/mpainter/scripts/host/gpu-env.sh"
phase="${BTS_UNLOAD_TEST_PHASE:-all}"
if [[ "$phase" == all ]]; then
xvfb-run -a -s '-screen 0 1600x1000x24' "$root/build/linux-native/bin/bts-theme-tests"
xvfb-run -a "$root/build/linux-native/bin/bts-texture-set-residency-tests"
fi
if [[ "$phase" != workspace ]]; then
xvfb-run -a -s '-screen 0 1600x1000x24' env BTS_UNLOAD_REGRESSION=1 timeout 240 bash -c 'export BTS_PRIVATE_XVFB_PID=$(cat "/tmp/.X${DISPLAY#:}-lock"); exec "$BTS_HOST_EXECUTABLE" --headless-viewport-test "$1"' unloadfix "$out"
cp "$out/unload-regression.json" "$root/mpainter/docs/unloadfix/native.json"
fi
if [[ "$phase" != native ]]; then
xvfb-run -a -s '-screen 0 1600x1000x24' env BTS_CHECKBOX_DEMO=1 timeout 240 bash -c 'export BTS_PRIVATE_XVFB_PID=$(cat "/tmp/.X${DISPLAY#:}-lock"); exec "$BTS_HOST_EXECUTABLE" --workspace --gui-fix-demo "$1" --gui-fix-mesh "$BTS_ROOT/mpainter/docs/checkbox/residency.obj" --gui-fix-resolution 128' unloadfix "$out"
cp "$out/checkbox-regression.json" "$root/mpainter/docs/unloadfix/workspace.json"
cp "$out/texturesets-tab.png" "$root/unloadfix.png"
fi
