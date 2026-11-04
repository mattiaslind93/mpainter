#!/usr/bin/env bash
set -euo pipefail
test -f /run/.containerenv
ulimit -c 0
root=/mnt/mpainter-phase1
out="${BTS_SHADERBALL_OUTPUT:-$root/build/shaderball-native}"
mkdir -p "$out" "$root/build/tmp"
export TMPDIR="$root/build/tmp"
unset WAYLAND_DISPLAY
export QT_QPA_PLATFORM=xcb QT_PLUGIN_PATH="$root/toolchains/qt/plugins"
export LD_LIBRARY_PATH="$root/toolchains/qt/lib:$root/build/runtime/lib"
export OMP_NUM_THREADS=4 OPENBLAS_NUM_THREADS=1 BTS_HOSTED_SHADER_WORKERS=0
export BTS_ROOT="$root" BTS_NATIVE_MATERIAL_HOST_QUEUE_TEST=1 BTS_NATIVE_THUMBNAIL_TEST=1
export BTS_HOST_EXECUTABLE="${BTS_HOST_EXECUTABLE:-$root/build/linux-native/bin/mpainter}"
export BTS_NATIVE_THUMBNAIL_SOURCE="$out/source/known-previews.blend" BTS_NATIVE_THUMBNAIL_OUTPUT="$out"
export XDG_CONFIG_HOME="$out/mpainter-test-sessions/config" XDG_DATA_HOME="$out/data" XDG_CACHE_HOME="$out/cache"
export BTS_RECOVERY_DIR="$out/recovery" BTS_RESOURCE_CACHE="$out/resources"
export BTS_ENVIRONMENT="$root/build/runtime/5.2/datafiles/studiolights/world/default-studio.hdr"
source "$root/mpainter/scripts/host/gpu-env.sh"
# Two tiny material graphs; source preview is 13x9, project rendering is 128px.
# The thumbnail test returns before the generic 4096px mip stress test.
xvfb-run -a -s '-screen 0 1600x1000x24' timeout 300 bash -euo pipefail -c '
  export BTS_PRIVATE_XVFB_PID=$(cat "/tmp/.X${DISPLAY#:}-lock")
  "$1" --background --factory-startup --threads 4 --python "$BTS_ROOT/mpainter/scripts/host/generate-native-thumbnail-fixture.py" -- "$BTS_NATIVE_THUMBNAIL_SOURCE" > "$2/fixture.log" 2>&1
  "$BTS_HOST_EXECUTABLE" --headless-viewport-test "$2" > "$2/warm.log" 2>&1
  cp "$2/native-host-queue.json" "$2/warm.json"
  mv "$2/source" "$2/source-hidden"
  export BTS_NATIVE_THUMBNAIL_COLD=1 BTS_RESOURCE_CACHE="$2/cold-resources" XDG_CACHE_HOME="$2/cold-cache"
  "$BTS_HOST_EXECUTABLE" --headless-viewport-test "$2" > "$2/cold.log" 2>&1
  cp "$2/native-host-queue.json" "$2/cold.json"
' shaderball-native "${BTS_TEST_BLENDER:-/home/mlind/bts/build/rocky9/bin/blender}" "$out"
python3 - "$out" <<'PY'
import json,sys
from pathlib import Path
p=Path(sys.argv[1]);warm=json.loads((p/'warm.json').read_text())['native_thumbnails'];cold=json.loads((p/'cold.json').read_text())['native_thumbnails']
for key in ['saved_preview_exact','no_preview_evaluated_channels','legacy_shelf_generation','legacy_preview_error_resolved','published_asset_reensure_no_gpu_work']: assert warm[key], key
for key in ['fresh_process_project_open','source_absent','saved_preview_exact','generated_preview_exact']: assert cold[key], key
# A constant red native graph must include the JL01 base, below the sphere.
from PIL import Image
image=Image.open(p/'evaluated-preview.png').convert('RGB')
assert image.size==(64,64)
base=[image.getpixel((x,y)) for y in range(58,62) for x in range(22,42)]
assert sum(r>g*2 and r>b*2 for r,g,b in base)>20, 'Native preview lost shaderball base'
print('Native shaderball previews: evaluated channels, authored images, legacy generation, source-free reopen PASS')
PY
