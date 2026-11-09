#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
# Invoke inside distrobox bts-build. Run one test/benchmark at a time.
root="${BTS_ROOT:-/mnt/mpainter-phase1}"
out="${1:-$root/shots/gpubake}"
mkdir -p "$out"
unset WAYLAND_DISPLAY
export QT_QPA_PLATFORM=xcb QT_PLUGIN_PATH="$root/toolchains/qt/plugins"
export LD_LIBRARY_PATH="$root/toolchains/qt/lib:$root/build/runtime/lib"
export OMP_NUM_THREADS=6 OPENBLAS_NUM_THREADS=1
export XDG_CONFIG_HOME="$root/build/mpainter-test-sessions/gpubake/config"
export XDG_CACHE_HOME="$root/build/mpainter-test-sessions/gpubake/cache"
export XDG_DATA_HOME="$root/build/mpainter-test-sessions/gpubake/data"
export BTS_RECOVERY_DIR="$root/build/mpainter-test-sessions/gpubake/recovery"
export BTS_ENVIRONMENT="$root/default-studio.hdr"
export BTS_GPU_BAKE_BVH_TEST=1
if [[ ${BTS_GPU_PHASE1_TEST:-1} == 1 ]]; then export BTS_GPU_PHASE1_TEST=1; else unset BTS_GPU_PHASE1_TEST; fi
export BTS_GPU_PHASE1_MESH="${BTS_GPU_PHASE1_MESH:-$root/build/blender/tests/files/io_tests/obj/makehuman.obj}"
export BTS_ROOT="$root"
source "$root/mpainter/scripts/host/gpu-env.sh"
# Affinity bounds TBB as well as OpenMP without touching other user processes.
cpus=$(python3 -c 'import os; print(",".join(map(str, sorted(os.sched_getaffinity(0))[:6])))')
xvfb-run -a -s '-screen 0 1400x1100x24' bash -c 'export BTS_PRIVATE_XVFB_PID=$(cat "/tmp/.X${DISPLAY#:}-lock"); exec "$@"' gpubake \
 taskset -c "$cpus" "${BTS_HOST_EXECUTABLE:-$root/build/linux-native/bin/mpainter}" --no-splash --headless-viewport-test "$out"
