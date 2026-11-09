#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
BTS_ROOT=${BTS_ROOT:-$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../../.." && pwd -P)}
BTS_QT_JOBS=${BTS_QT_JOBS:-4}
version=6.8.3
archive="qtbase-everywhere-src-$version.tar.xz"
mkdir -p "$BTS_ROOT/downloads" "$BTS_ROOT/qt"
if [[ ! -f "$BTS_ROOT/downloads/$archive" ]]; then
  curl --fail --location --retry 3 \
    "https://download.qt.io/archive/qt/6.8/$version/submodules/$archive" \
    --output "$BTS_ROOT/downloads/$archive"
fi
printf '%s  %s\n' 56001b905601bb9023d399f3ba780d7fa940f3e4861e496a7c490331f49e0b80 \
  "$BTS_ROOT/downloads/$archive" | sha256sum --check
if [[ ! -d "$BTS_ROOT/qt/qtbase-everywhere-src-$version" ]]; then
  tar -xf "$BTS_ROOT/downloads/$archive" -C "$BTS_ROOT/qt"
fi
python3 "$(dirname "$0")/apply-qt-patches.py" --source "$BTS_ROOT/qt/qtbase-everywhere-src-$version"
export CCACHE_DIR="$BTS_ROOT/build/ccache"
export CCACHE_MAXSIZE=30G
ccache --set-config "base_dir=$BTS_ROOT"
cmake --fresh -S "$BTS_ROOT/qt/qtbase-everywhere-src-$version" \
  -B "$BTS_ROOT/build/qt-$version" -G Ninja \
  -DCMAKE_INSTALL_PREFIX="$BTS_ROOT/qt/$version" \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER_LAUNCHER=ccache \
  -DCMAKE_CXX_COMPILER_LAUNCHER=ccache -DQT_BUILD_EXAMPLES=OFF \
  -DQT_BUILD_TESTS=OFF -DFEATURE_xcb=ON -DFEATURE_opengl_desktop=ON \
  -DFEATURE_fontconfig=ON
cmake --build "$BTS_ROOT/build/qt-$version" --parallel "$BTS_QT_JOBS"
cmake --install "$BTS_ROOT/build/qt-$version"
"$BTS_ROOT/qt/$version/bin/qmake" -query QT_VERSION
printf '%s | Qt %s byggd från verifierad upstream-källa. Nästa: länka hosten mot VFX-plattformens Qt 6.8-serie.\n' \
  "$(date -Is)" "$version" >> "$BTS_ROOT/supervisor/status.md"
