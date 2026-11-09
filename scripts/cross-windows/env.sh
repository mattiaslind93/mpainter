#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Source only inside the build distrobox, through the simple bind mount.
set -euo pipefail
: "${MP_WIN_WORK:=/mnt/mpainter-phase1}"
[[ -e /run/.containerenv ]] || { echo 'Run inside a distrobox/container.' >&2; exit 1; }
[[ "$MP_WIN_WORK" == /* && "$MP_WIN_WORK" != *\\* && "$MP_WIN_WORK" != *' '* ]] || { echo 'Use an absolute bind mount without spaces/backslashes.' >&2; exit 1; }
memory_cap=$(cat /sys/fs/cgroup/memory.max)
[[ "$memory_cap" =~ ^[0-9]+$ ]] && (( memory_cap <= 32212254720 )) || {
  echo 'Cap this container at 30 GiB or less before running the cross-build scripts.' >&2
  exit 1
}
export MP_WIN_WORK
export MP_WIN_SDK="$MP_WIN_WORK/toolchains/windows/msvc"
export MP_WIN_QT="$MP_WIN_WORK/toolchains/windows/qt/6.8.3/msvc2022_64"
export MP_WIN_SCRIPTS="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
export TMPDIR="$MP_WIN_WORK/build/windows/tmp"
export XDG_CACHE_HOME="$MP_WIN_WORK/cache/windows"
export XDG_CONFIG_HOME="$MP_WIN_WORK/build/windows/xdg/config"
export XDG_DATA_HOME="$MP_WIN_WORK/build/windows/xdg/data"
export XDG_STATE_HOME="$MP_WIN_WORK/build/windows/xdg/state"
export PIP_CACHE_DIR="$XDG_CACHE_HOME/pip"
: "${MP_WIN_JOBS:=6}"
[[ "$MP_WIN_JOBS" =~ ^[1-6]$ ]] || { echo 'MP_WIN_JOBS must be 1..6.' >&2; exit 1; }
export MP_WIN_JOBS
export CMAKE_BUILD_PARALLEL_LEVEL="$MP_WIN_JOBS"
export RAYON_NUM_THREADS=6
export OMP_NUM_THREADS="$MP_WIN_JOBS"
export WINEPREFIX="$MP_WIN_WORK/build/windows/wine-prefix"
export WINEARCH=win64
# Blender searches executable suffixes through the Windows environment contract.
export PATHEXT="${PATHEXT:-.COM;.EXE;.BAT;.CMD}"
export WINEDEBUG=-all WINEDLLOVERRIDES='mscoree,mshtml=;winemenubuilder.exe=d'
export QT_QPA_PLATFORM=offscreen LIBGL_ALWAYS_SOFTWARE=1
unset DISPLAY WAYLAND_DISPLAY
mkdir -p "$TMPDIR" "$XDG_CACHE_HOME" "$MP_WIN_WORK/logs/windows"
