#!/usr/bin/env bash
set -euo pipefail
test -f /run/.containerenv
ulimit -c 0
root=/mnt/mpainter-phase1
work="$root/build/polyhaven"
export TMPDIR="$root/build/tmp" OMP_NUM_THREADS=4 OPENBLAS_NUM_THREADS=1
export BTS_ROOT="$root" BTS_HOSTED_SHADER_WORKERS=0 QT_QPA_PLATFORM=xcb
export QT_PLUGIN_PATH="$root/toolchains/qt/plugins"
export LD_LIBRARY_PATH="$root/toolchains/qt/lib:$root/build/runtime/lib"
unset WAYLAND_DISPLAY
source "$root/mpainter/scripts/host/gpu-env.sh"
export XDG_CONFIG_HOME="$work/mpainter-test-sessions/config" XDG_DATA_HOME="$work/data" XDG_CACHE_HOME="$work/cache"
export BTS_RECOVERY_DIR="$work/recovery" BTS_RESOURCE_CACHE="$work/resources"
export BTS_ENVIRONMENT="$root/build/runtime/5.2/datafiles/studiolights/world/default-studio.hdr"
mkdir -p "$TMPDIR" "$work/source" "$work/logs"
case "${1:-generate}" in
 generate|regenerate)
  if [[ "${1:-generate}" == regenerate ]]; then
   export BTS_POLYHAVEN_REGENERATE=1
   test -n "${BTS_POLYHAVEN_SLUGS:-}" # explicitly select assets to overwrite
  fi
  python3 - "$work/mapping.json" "$root/mpainter/assets/materials/polyhaven/manifest.json" <<'PY'
import sys,json
from pathlib import Path
source=Path(sys.argv[1]);out=Path(sys.argv[2])
if not out.exists():out.write_text(source.read_text())
PY
  xvfb-run -a -s '-screen 0 1600x1000x24' bash -euo pipefail -c '
    export BTS_PRIVATE_XVFB_PID=$(cat "/tmp/.X${DISPLAY#:}-lock")
    python3 -c '\''import json,os;selected=os.environ.get("BTS_POLYHAVEN_SLUGS", "").split();print("\n".join(r["slug"] for r in json.load(open("'$work'/mapping.json")) if not selected or r["slug"] in selected))'\'' | while IFS= read -r slug; do
      if [ "${BTS_POLYHAVEN_REGENERATE:-0}" == 1 ] || [ ! -f "'$work'/source/$slug.png" ]; then
        timeout 180 /home/mlind/bts/build/rocky9/bin/blender --background --factory-startup --threads 4 --python "'$root'/mpainter/scripts/polyhaven/generate.py" -- "'$work'/mapping.json" "'$work'/textures" "'$work'/source" "'$root'/mpainter/assets/meshes/Shader_Ball_JL_01.obj" "$slug" > "'$work'/logs/$slug-render.log" 2>&1
      fi
      if [ "${BTS_POLYHAVEN_REGENERATE:-0}" == 1 ] || [ ! -f "'$root'/mpainter/assets/materials/polyhaven/native/$slug.json" ]; then
        BTS_POLYHAVEN_MATERIAL="$slug" timeout 180 "'$root'/build/linux-native/bin/mpainter" --no-splash --cpu-prepare-polyhaven "'$root'/mpainter/assets/materials/polyhaven" --polyhaven-source "'$work'/source" > "'$work'/logs/$slug-import.log" 2>&1
      fi
      echo "PREPARED $slug"
    done
  '
  ;;
 cpu)
  xvfb-run -a -s '-screen 0 1600x1000x24' timeout 120 bash -euo pipefail -c '
    export BTS_PRIVATE_XVFB_PID=$(cat "/tmp/.X${DISPLAY#:}-lock")
    "$1/build/linux-native/bin/bts-asset-tests"
    "$1/build/linux-native/bin/mpainter" --no-splash --cpu-asset-dragdrop-test "$1/build/polyhaven/cpu-workspace"
  ' polyhaven-cpu "$root"
  ;;
 test-resident)
  out="${BTS_POLYHAVEN_OUTPUT:-$work/resident}"
  mkdir -p "$out"
  xvfb-run -a -s '-screen 0 1600x1000x24' env BTS_NATIVE_FRAGMENT_NORMAL_TEST=1 BTS_NATIVE_FRAGMENT_RESIDENT_ONLY=1 timeout 180 bash -c 'export BTS_PRIVATE_XVFB_PID=$(cat "/tmp/.X${DISPLAY#:}-lock"); exec "$1" --no-splash --headless-viewport-test "$2"' polyhaven "${BTS_HOST_EXECUTABLE:-$root/build/linux-native/bin/mpainter}" "$out"
  ;;
 test|test-mapping)
  export BTS_POLYHAVEN_DEMO=1 BTS_BJ_PROJECTION_UI=1
  if [[ "$1" == test ]]; then export BTS_POLYHAVEN_DETAIL="${BTS_POLYHAVEN_DETAIL:-Cherry Wood}"; else unset BTS_POLYHAVEN_DETAIL; fi
  out="${BTS_POLYHAVEN_OUTPUT:-$work/ui}"
  mkdir -p "$out"
  export XDG_CONFIG_HOME="$out/mpainter-test-sessions/config" XDG_DATA_HOME="$out/data" XDG_CACHE_HOME="$out/cache" BTS_RESOURCE_CACHE="$out/resources"
  cd /tmp
  xvfb-run -a -s '-screen 0 1600x1000x24' timeout 480 bash -c 'export BTS_PRIVATE_XVFB_PID=$(cat "/tmp/.X${DISPLAY#:}-lock"); exec "$1" --no-splash --workspace --gui-fix-demo "$2"' polyhaven "${BTS_HOST_EXECUTABLE:-$root/build/linux-native/bin/mpainter}" "$out"
  ;;
esac
