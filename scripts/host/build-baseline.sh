#!/usr/bin/env bash
set -euo pipefail
BTS_ROOT=${BTS_ROOT:-$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../../.." && pwd -P)}
BTS_JOBS=${BTS_JOBS:-6}
source_dir="$BTS_ROOT/blender"
build_dir="$BTS_ROOT/build/baseline"
project_dir="$(cd "$(dirname "$0")/../.." && pwd)"
pin=fbe6228777e7d9afefcd61a413844e790ae75db7

test "$(git -C "$source_dir" rev-parse HEAD)" = "$pin"
git -C "$source_dir" diff --exit-code --quiet
git -C "$source_dir" diff --cached --exit-code --quiet
export CCACHE_DIR="$BTS_ROOT/build/ccache"
export CCACHE_MAXSIZE=30G
ccache --set-config "base_dir=$BTS_ROOT"
cmake -S "$source_dir" -B "$build_dir" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DWITH_COMPILER_CCACHE=ON
cmake --build "$build_dir" --parallel "$BTS_JOBS" --target install
source "$project_dir/scripts/host/gpu-env.sh"
"$build_dir/bin/blender" --version
# An empty value forces GHOST's X11 fallback. Unsetting it still lets libwayland
# discover wayland-0, whose EGL context fails in this NVIDIA distrobox session.
env WAYLAND_DISPLAY= "$build_dir/bin/blender" --background --factory-startup --gpu-backend opengl \
  --python-exit-code 1 --python "$project_dir/scripts/host/baseline-fixture.py" \
  -- --output "$BTS_ROOT/build/baseline-demo"
git -C "$source_dir" diff --exit-code --quiet
printf '%s | Oförändrad Blender från pinnen byggd och EEVEE-fixture verifierad. Nästa: Qt-host i samma CMake-graf och PID.\n' \
  "$(date -Is)" >> "$BTS_ROOT/supervisor/status.md"
