#!/usr/bin/env bash
set -euo pipefail
BTS_ROOT=${BTS_ROOT:-$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../../.." && pwd -P)}
project_dir="$(cd "$(dirname "$0")/../.." && pwd)"
qt_prefix="${BTS_QT_PREFIX:-$project_dir/../toolchains/qt}"
cmake -S "$project_dir/scripts/host/qt-probe" -B "$BTS_ROOT/build/qt-gpu-probe" -G Ninja \
  -DCMAKE_PREFIX_PATH="$qt_prefix" -DCMAKE_BUILD_TYPE=Release
cmake --build "$BTS_ROOT/build/qt-gpu-probe" --parallel 1
source "$project_dir/scripts/host/gpu-env.sh"
export QT_QPA_PLATFORM=xcb
export QT_PLUGIN_PATH="$qt_prefix/plugins"
export LD_LIBRARY_PATH="$qt_prefix/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
exec "$BTS_ROOT/build/qt-gpu-probe/qt-gpu-probe" "${1:-$BTS_ROOT/build/qt-demo}"
