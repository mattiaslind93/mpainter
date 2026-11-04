#!/usr/bin/env bash
# Build and install the pinned native Cycles kernels in the existing host CMake graph.
set -euo pipefail
BTS_ROOT=${BTS_ROOT:-$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../../.." && pwd -P)}
BTS_JOBS=${BTS_JOBS:-6}
project_dir="$(cd "$(dirname "$0")/../.." && pwd)"
source "$project_dir/scripts/host/gpu-env.sh"
: "${CUDA_BIN_PATH:?Run scripts/host/setup-cuda.py first, or set CUDA_BIN_PATH}"
test -x "$CUDA_BIN_PATH/nvcc"
bts_cuda_compiler=${BTS_CUDA_HOST_COMPILER:-/usr/bin/g++-14}
test -x "$bts_cuda_compiler"
cmake -S "$BTS_ROOT/blender" -B "$BTS_ROOT/build/host" \
  -DWITH_CYCLES_CUDA_BINARIES=ON \
  -DCYCLES_CUDA_BINARIES_ARCH="${BTS_CUDA_ARCH:-sm_86}" \
  -DCUDA_TOOLKIT_ROOT_DIR="$(dirname "$CUDA_BIN_PATH")" \
  -DCUDA_NVCC_EXECUTABLE="$CUDA_BIN_PATH/nvcc" \
  -DCUDA_HOST_COMPILER="$bts_cuda_compiler"
cmake --build "$BTS_ROOT/build/host" --parallel "$BTS_JOBS" --target bts-host cycles_kernel_cuda
cmake --install "$BTS_ROOT/build/host"
