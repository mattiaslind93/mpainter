#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
# All application-owned state is confined to the supplied output directory.
set -euo pipefail
probe=${1:?Probe executable required}
runtime=${2:?Blender.app/Contents/Resources directory required}
output=${3:?Output directory required}
mkdir -p "$output"
output=$(cd "$output" && pwd)
export QT_QPA_PLATFORM=offscreen
export QT_QPA_OFFSCREEN_NO_GLX=1
export XDG_CONFIG_HOME="$output/config"
export XDG_DATA_HOME="$output/data"
export XDG_CACHE_HOME="$output/cache"
export MPAINTER_CACHE_DIR="$output/blender-cache"
export BLENDER_USER_RESOURCES="$output/blender-user"
export BLENDER_USER_EXTENSIONS="$output/blender-extensions"
export BLENDER_USER_CONFIG="$output/blender-config"
export BLENDER_USER_SCRIPTS="$output/blender-scripts"
export BLENDER_USER_DATAFILES="$output/blender-data"
export BLENDER_SYSTEM_RESOURCES="$runtime/5.2"
export BLENDER_SYSTEM_SCRIPTS="$runtime/5.2/scripts"
export BLENDER_SYSTEM_DATAFILES="$runtime/5.2/datafiles"
export BLENDER_SYSTEM_PYTHON="$runtime/5.2/python"
export TMPDIR="$output/tmp"
export CFFIXED_USER_HOME="$output/foundation-home"
export OMP_NUM_THREADS=6
export OPENBLAS_NUM_THREADS=6
mkdir -p "$TMPDIR" "$XDG_CACHE_HOME" "$CFFIXED_USER_HOME" \
  "$XDG_CONFIG_HOME" "$XDG_DATA_HOME" "$MPAINTER_CACHE_DIR" \
  "$BLENDER_USER_CONFIG" "$BLENDER_USER_SCRIPTS" "$BLENDER_USER_DATAFILES" \
  "$BLENDER_USER_RESOURCES" "$BLENDER_USER_EXTENSIONS"
exec nice -n 15 "$probe" --self-test "$output"
