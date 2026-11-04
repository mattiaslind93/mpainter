#!/bin/bash
# Host entrypoint. Every Blender/app execution uses bts-build and its own Xvfb.
set -euo pipefail
material_root="${MPAINTER_TEST_ROOT:-/mnt/mpainter-phase1}"
material_fixtures="${MPAINTER_TEST_FIXTURES:-$material_root/logs/material-import/fixtures}"
material_launcher="${MPAINTER_TEST_LAUNCHER:-$material_root/build/cycles-driver-probe/AppRun}"
material_blender="${MPAINTER_TEST_BLENDER:-/home/mlind/.local/bin/blender}"
material_wrapper="$material_root/mpainter/scripts/host/run-parity-headless.sh"
material_generate=0
material_rapid=0
for argument in "$@"; do
 case "$argument" in --generate) material_generate=1;; --rapid) material_rapid=1;; *) echo "Usage: $0 [--generate] [--rapid]" >&2; exit 2;; esac
done
material_output="${MPAINTER_TEST_OUTPUT:-$material_root/logs/material-import/results}"
material_extra=()
if ((material_rapid)); then
 material_output="${MPAINTER_TEST_OUTPUT:-$material_root/logs/material-import/rapid-results}"
 material_extra+=(BTS_MATERIAL_IMPORT_RAPID_SWITCH_TEST=1)
fi
if ((material_generate)); then
 distrobox enter bts-build -- env -u WAYLAND_DISPLAY -u LD_LIBRARY_PATH \
  xvfb-run -a "$material_wrapper" "$material_blender" -b --threads 2 \
  --python "$material_root/mpainter/native/tests/fixtures/create_normal_materials.py" -- "$material_fixtures"
fi
distrobox enter bts-build -- env -u WAYLAND_DISPLAY -u LD_LIBRARY_PATH \
 "${material_extra[@]}" BTS_NATIVE_THUMBNAIL_TEST=1 BTS_NATIVE_MATERIAL_HOST_QUEUE_TEST=1 \
 "BTS_MATERIAL_IMPORT_CASES=$material_fixtures/cases.json" "BTS_MATERIAL_IMPORT_MESH=$material_fixtures/sphere.obj" \
 "BTS_NATIVE_THUMBNAIL_OUTPUT=$material_output" \
 "BTS_ENVIRONMENT=$material_root/build/runtime/5.2/datafiles/studiolights/world/default-studio.hdr" \
 xvfb-run -a "$material_wrapper" bash -c \
 'source "$1"; unset WAYLAND_DISPLAY; export VGL_EGLLIB=/usr/lib64/libEGL.so.1; exec vglrun -nodl -d egl -c proxy "$2" --headless-viewport-test "$3"' \
 bash "$material_root/mpainter/scripts/host/gpu-env.sh" "$material_launcher" "$material_output"
