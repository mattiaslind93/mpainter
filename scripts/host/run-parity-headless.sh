#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Invoke inside bts-build, as a child of: env -u WAYLAND_DISPLAY xvfb-run -a
set -euo pipefail
unset WAYLAND_DISPLAY
case "${DISPLAY:-}" in :0|:0.*|:1|:1.*|'') echo 'A private Xvfb display is required' >&2; exit 1;; esac
[[ "${CONTAINER_ID:-${CONTAINER_NAME:-}}" == *bts-build* || -e /run/.containerenv ]] || { echo 'Run inside bts-build' >&2; exit 1; }
parity_root="$(cd -- "$(dirname -- "$0")/../../.." && pwd)/build/mpainter-test-sessions/parity-$$"
mkdir -p "$parity_root"/{config,data,cache,state,tmp,recovery,resources}
export XDG_CONFIG_HOME="$parity_root/config" XDG_DATA_HOME="$parity_root/data" XDG_CACHE_HOME="$parity_root/cache" XDG_STATE_HOME="$parity_root/state" TMPDIR="$parity_root/tmp"
export BTS_ROOT="$parity_root" BTS_RECOVERY_DIR="$parity_root/recovery" BTS_RESOURCE_CACHE="$parity_root/resources"
# Share only compiler caches across this workspace's private test processes.
# NVIDIA does not create a missing cache directory itself.
parity_gpu_cache="$(dirname -- "$parity_root")/parity-gpu-cache"
mkdir -p "$parity_gpu_cache"/{nvidia,mesa,cuda}
export __GL_SHADER_DISK_CACHE_PATH="$parity_gpu_cache/nvidia" MESA_SHADER_CACHE_DIR="$parity_gpu_cache/mesa" CUDA_CACHE_PATH="$parity_gpu_cache/cuda"
parity_display_number="${DISPLAY#:}"
parity_display_number="${parity_display_number%%.*}"
export BTS_PRIVATE_XVFB_PID="$(tr -d '[:space:]' < "/tmp/.X${parity_display_number}-lock")"
export QT_QPA_PLATFORM=xcb PYTHONDONTWRITEBYTECODE=1
exec "$@"
