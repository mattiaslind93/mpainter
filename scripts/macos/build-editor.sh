#!/usr/bin/env bash
set -euo pipefail
work="$(cd "$(dirname "$0")/../../.." && pwd)"
export GIT_CONFIG_GLOBAL="$work/toolchains/gitconfig" GIT_CONFIG_NOSYSTEM=1 TMPDIR="$work/tmp"
export PATH="$work/toolchains/python/bin:$PATH"
# Reuse the proven headless Blender graph. Build the real host alongside probe.
nice -n 15 cmake -S "$work/blender" -B "$work/build-metal" \
 -DBTS_MACOS_METAL_PROBE=ON -DBTS_MACOS_METAL_EDITOR=ON \
 "-DCMAKE_PREFIX_PATH=$work/toolchains/qt/6.8.3/macos;$work/toolchains/macos-deps" \
 "-DLZ4_INCLUDE_DIR=$work/toolchains/macos-deps/include" "-DLZ4_LIBRARY=$work/toolchains/macos-deps/lib/liblz4.dylib" \
 "-DHDF5_ROOT=$work/toolchains/macos-deps" \
 "-Dassimp_DIR=$work/toolchains/macos-deps/lib/cmake/assimp-5.4"
nice -n 15 cmake --build "$work/build-metal" --parallel 6 --target bts-host
