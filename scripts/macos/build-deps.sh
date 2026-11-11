#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Pinned, workspace-local arm64 dependencies. Never use a package manager/system prefix.
set -euo pipefail
work="$(cd "$(dirname "$0")/../../.." && pwd)"
export GIT_CONFIG_GLOBAL="$work/toolchains/gitconfig" GIT_CONFIG_NOSYSTEM=1 TMPDIR="$work/tmp"
mkdir -p "$work/cache/metal2" "$work/dependency-source"
cd "$work/cache/metal2"
for package in assimp hdf5; do
  if [[ "$package" == assimp ]]; then
    archive=assimp-5.4.3.tar.gz
    url=https://github.com/assimp/assimp/archive/refs/tags/v5.4.3.tar.gz
    hash=66dfbaee288f2bc43172440a55d0235dfc7bf885dda6435c038e8000e79582cb
  else
    archive=hdf5-1.14.6.tar.gz
    url=https://github.com/HDFGroup/hdf5/archive/refs/tags/hdf5_1.14.6.tar.gz
    hash=09ee1c671a87401a5201c06106650f62badeea5a3b3941e9b1e2e1e08317357f
  fi
  [[ -f "$archive" ]] || curl -LfsS --retry 2 --max-time 240 "$url" -o "$archive"
  printf '%s  %s\n' "$hash" "$archive" | shasum -a 256 -c -
done
[[ -d "$work/dependency-source/assimp-5.4.3" ]] || tar -xzf assimp-5.4.3.tar.gz -C "$work/dependency-source"
[[ -d "$work/dependency-source/hdf5-hdf5_1.14.6" ]] || tar -xzf hdf5-1.14.6.tar.gz -C "$work/dependency-source"
common=(-G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64
 -DCMAKE_OSX_DEPLOYMENT_TARGET=12.0 "-DCMAKE_INSTALL_PREFIX=$work/toolchains/macos-deps"
 -DBUILD_SHARED_LIBS=ON -DBUILD_TESTING=OFF)
nice -n 15 cmake -S "$work/dependency-source/assimp-5.4.3" -B "$work/build-assimp" "${common[@]}" -DASSIMP_BUILD_TESTS=OFF -DASSIMP_BUILD_ASSIMP_TOOLS=OFF -DASSIMP_BUILD_ZLIB=OFF -DASSIMP_WARNINGS_AS_ERRORS=OFF -DASSIMP_INSTALL_PDB=OFF
nice -n 15 cmake --build "$work/build-assimp" --parallel 6
nice -n 15 cmake --install "$work/build-assimp"
nice -n 15 cmake -S "$work/dependency-source/hdf5-hdf5_1.14.6" -B "$work/build-hdf5" "${common[@]}" -DHDF5_BUILD_TOOLS=OFF -DHDF5_BUILD_EXAMPLES=OFF -DHDF5_BUILD_CPP_LIB=OFF -DHDF5_BUILD_FORTRAN=OFF -DHDF5_BUILD_HL_LIB=OFF -DHDF5_ENABLE_Z_LIB_SUPPORT=OFF -DHDF5_ENABLE_SZIP_SUPPORT=OFF -DHDF5_NO_PACKAGES=ON -DBUILD_STATIC_LIBS=OFF
nice -n 15 cmake --build "$work/build-hdf5" --parallel 6
nice -n 15 cmake --install "$work/build-hdf5"
# LZ4 is linked by the SPSM decoder; do not rely on a machine's Homebrew prefix.
[[ -f lz4-1.10.0.tar.gz ]] || curl -LfsS --retry 2 --max-time 240 https://github.com/lz4/lz4/archive/refs/tags/v1.10.0.tar.gz -o lz4-1.10.0.tar.gz
printf '%s\n' '537512904744b35e232912055ccf8ec66d768639ff3abe5788d90d792ec5f48b  lz4-1.10.0.tar.gz' | shasum -a 256 -c -
[[ -d "$work/dependency-source/lz4-1.10.0" ]] || tar -xzf lz4-1.10.0.tar.gz -C "$work/dependency-source"
nice -n 15 cmake -S "$work/dependency-source/lz4-1.10.0/build/cmake" -B "$work/build-lz4" "${common[@]}" -DLZ4_BUILD_CLI=OFF -DLZ4_BUILD_LEGACY_LZ4C=OFF
nice -n 15 cmake --build "$work/build-lz4" --parallel 6
nice -n 15 cmake --install "$work/build-lz4"
