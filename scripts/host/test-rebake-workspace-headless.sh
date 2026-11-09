#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
# Run inside bts-build, after the CPU test, with no other heavy workload running.
ulimit -c 0
root="${BTS_ROOT:-/mnt/mpainter-phase1}"
out="${1:-$root/build/crashcurv/workspace}"
mkdir -p "$out"
unset WAYLAND_DISPLAY
export QT_QPA_PLATFORM=xcb QT_PLUGIN_PATH="$root/toolchains/qt/plugins"
export LD_LIBRARY_PATH="$root/toolchains/qt/lib:$root/build/runtime/lib"
export OMP_NUM_THREADS=6 OPENBLAS_NUM_THREADS=1
export XDG_CONFIG_HOME="$root/build/mpainter-test-sessions/crashcurv/config" XDG_CACHE_HOME="$out/cache" XDG_DATA_HOME="$out/data" BTS_RECOVERY_DIR="$root/build/mpainter-test-sessions/crashcurv/recovery"
export BTS_ENVIRONMENT="$root/mpainter/assets/environments/default-studio.hdr"
export BTS_ROOT="$root" BTS_BAKE_FIX_DEMO=1 BTS_REBAKE_WORKSPACE=1 BTS_HOSTED_SHADER_WORKERS=0
source "$root/mpainter/scripts/host/gpu-env.sh"
xvfb-run -a -s '-screen 0 1600x1000x24' timeout 600 bash -c 'export BTS_PRIVATE_XVFB_PID=$(cat "/tmp/.X${DISPLAY#:}-lock"); exec "$@"' rebake "${BTS_HOST_EXECUTABLE:-$root/build/linux-native/bin/mpainter}" --no-splash --workspace --gui-fix-demo "$out" --gui-fix-mesh "$root/mpainter/assets/meshes/Shader_Ball_JL_01.obj" --gui-fix-resolution 128
python3 "$root/mpainter/scripts/host/verify-rebake-workspace.py" "$out"
