#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/env.sh"
# Run sequentially: never overlap dependency or application builds.
cd "$MP_WIN_WORK/cache/windows"
printf '%s\n' \
 '66dfbaee288f2bc43172440a55d0235dfc7bf885dda6435c038e8000e79582cb  assimp-5.4.3.tar.gz' \
 '09ee1c671a87401a5201c06106650f62badeea5a3b3941e9b1e2e1e08317357f  hdf5-1.14.6.tar.gz' \
 '537512904744b35e232912055ccf8ec66d768639ff3abe5788d90d792ec5f48b  lz4-1.10.0.tar.gz' | sha256sum -c -
src="$MP_WIN_WORK/build/windows/dependency-source"
mkdir -p "$src"
[[ -d "$src/assimp-5.4.3" ]] || tar -xzf assimp-5.4.3.tar.gz -C "$src"
[[ -d "$src/hdf5-hdf5_1.14.6" ]] || tar -xzf hdf5-1.14.6.tar.gz -C "$src"
[[ -d "$src/lz4-1.10.0" ]] || tar -xzf lz4-1.10.0.tar.gz -C "$src"
common=(-G Ninja "-DCMAKE_TOOLCHAIN_FILE=$MP_WIN_SCRIPTS/toolchain.cmake"
  -DCMAKE_BUILD_TYPE=Release "-DCMAKE_INSTALL_PREFIX=$MP_WIN_WORK/toolchains/windows/deps"
  -DBUILD_SHARED_LIBS=ON -DBUILD_TESTING=OFF)
cmake -S "$src/assimp-5.4.3" -B "$MP_WIN_WORK/build/windows/assimp" "${common[@]}" \
  -DASSIMP_BUILD_TESTS=OFF -DASSIMP_BUILD_ASSIMP_TOOLS=OFF -DASSIMP_BUILD_ZLIB=ON \
  -DASSIMP_WARNINGS_AS_ERRORS=OFF -DASSIMP_INSTALL_PDB=OFF
cmake --build "$MP_WIN_WORK/build/windows/assimp" --parallel 6
cmake --install "$MP_WIN_WORK/build/windows/assimp" --config Release
cmake -S "$src/hdf5-hdf5_1.14.6" -B "$MP_WIN_WORK/build/windows/hdf5" "${common[@]}" \
  -DHDF5_BUILD_TOOLS=OFF -DHDF5_BUILD_EXAMPLES=OFF -DHDF5_BUILD_CPP_LIB=OFF \
  -DHDF5_BUILD_FORTRAN=OFF -DHDF5_BUILD_HL_LIB=OFF -DHDF5_ENABLE_Z_LIB_SUPPORT=OFF \
  -DHDF5_ENABLE_SZIP_SUPPORT=OFF -DHDF5_NO_PACKAGES=ON -DBUILD_STATIC_LIBS=OFF
cmake --build "$MP_WIN_WORK/build/windows/hdf5" --parallel 6
cmake --install "$MP_WIN_WORK/build/windows/hdf5" --config Release
cmake -S "$src/lz4-1.10.0/build/cmake" -B "$MP_WIN_WORK/build/windows/lz4" "${common[@]}" \
  -DLZ4_BUILD_CLI=OFF -DLZ4_BUILD_LEGACY_LZ4C=OFF
cmake --build "$MP_WIN_WORK/build/windows/lz4" --parallel 6
cmake --install "$MP_WIN_WORK/build/windows/lz4" --config Release
