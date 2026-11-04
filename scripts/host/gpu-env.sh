#!/usr/bin/env bash
# Source inside bts-build to use the host NVIDIA userspace with its matching kernel driver.
set -e
BTS_ROOT=${BTS_ROOT:-$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../../.." && pwd -P)}
export __GL_SYNC_TO_VBLANK=0
if [[ -f /run/host/usr/lib64/libGLX_nvidia.so.0 ]]; then
  BTS_DRIVER_DIR="$BTS_ROOT/build/nvidia-driver"
  mkdir -p "$BTS_DRIVER_DIR"
  for bts_library in /run/host/usr/lib64/libcuda.so* /run/host/usr/lib64/libnvidia-*.so* /run/host/usr/lib64/libGLX_nvidia.so* /run/host/usr/lib64/libEGL_nvidia.so*; do
    [[ -e "$bts_library" ]] || continue
    if [[ ! -e "$BTS_DRIVER_DIR/${bts_library##*/}" ]]; then
      ln -s "$bts_library" "$BTS_DRIVER_DIR/${bts_library##*/}"
    fi
  done
  export LD_LIBRARY_PATH="$BTS_DRIVER_DIR${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
  export __GLX_VENDOR_LIBRARY_NAME=nvidia
  # Qt presents on the input thread. NVIDIA vblank waits blocked it for ~104 ms
  # on XWayland despite swapInterval(0); the viewport already has a frame timer.
  # Keep an explicit user override (including 1) for other compositor setups.
  export __GL_SYNC_TO_VBLANK=${__GL_SYNC_TO_VBLANK:-0}
  export __EGL_VENDOR_LIBRARY_FILENAMES=/run/host/usr/share/glvnd/egl_vendor.d/10_nvidia.json
  # EGL's X11 platform needs the same NVIDIA GBM adapter as the host driver.
  # EGL is opt-in until Qt widget composition is verified on this driver stack.
  if [[ -r /run/host/usr/share/egl/egl_external_platform.d/20_nvidia_xlib.json &&
        -r /run/host/usr/lib64/gbm/nvidia-drm_gbm.so ]]; then
    export __EGL_EXTERNAL_PLATFORM_CONFIG_DIRS=${__EGL_EXTERNAL_PLATFORM_CONFIG_DIRS:-/run/host/usr/share/egl/egl_external_platform.d}
    export GBM_BACKENDS_PATH=${GBM_BACKENDS_PATH:-/run/host/usr/lib64/gbm}
    export GBM_BACKEND=${GBM_BACKEND:-nvidia-drm}
    if [[ ${BTS_XCB_EGL_EXPERIMENT:-0} == 1 && ${QT_QPA_PLATFORM:-xcb} == xcb && -r "$BTS_ROOT/qt/6.8.3/plugins/xcbglintegrations/libqxcb-egl-integration.so" ]]; then
      export QT_XCB_GL_INTEGRATION=${QT_XCB_GL_INTEGRATION:-xcb_egl}
    fi
  fi
fi

# The pinned Cycles backend can compile CUDA kernels locally when present.
# Keep a caller's toolkit/compiler choice; the approved local toolkit uses GCC 14.
if [[ -x "$BTS_ROOT/toolchains/cuda-13.2.1/bin/nvcc" ]]; then
  export CUDA_BIN_PATH=${CUDA_BIN_PATH:-"$BTS_ROOT/toolchains/cuda-13.2.1/bin"}
  if [[ -x /usr/bin/g++-14 ]]; then
    export CYCLES_CUDA_EXTRA_CFLAGS=${CYCLES_CUDA_EXTRA_CFLAGS:---compiler-bindir=/usr/bin/g++-14}
  fi
fi
