#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
BTS_ROOT=${BTS_ROOT:-$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../../.." && pwd -P)}
BTS_JOBS=${BTS_JOBS:-6}
project_dir="$(cd "$(dirname "$0")/../.." && pwd)"
# apply-fork.py checks the pinned Blender source and reviewed patch series.
# A receipt from a previous demo run is not a build prerequisite.
test -x "$BTS_ROOT/qt/6.8.3/bin/qmake"
python3 "$project_dir/scripts/host/apply-fork.py" "$BTS_ROOT/blender"
export CCACHE_DIR="$BTS_ROOT/build/ccache"
export CCACHE_MAXSIZE=30G
ccache --set-config "base_dir=$BTS_ROOT"
cmake -S "$BTS_ROOT/blender" -B "$BTS_ROOT/build/host" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DWITH_COMPILER_CCACHE=ON \
  -DCMAKE_PREFIX_PATH="$BTS_ROOT/qt/6.8.3" \
  -DBTS_HOST_SOURCE_DIR="$project_dir/native/host"
cmake --build "$BTS_ROOT/build/host" --parallel "$BTS_JOBS" --target install
