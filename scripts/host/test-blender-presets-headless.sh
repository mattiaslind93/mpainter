#!/usr/bin/env bash
# Reproducible offline proof. Run inside distrobox bts-build; never a desktop display.
set -euo pipefail
test -f /run/.containerenv
ulimit -c 0
root=${BTS_ROOT:-/mnt/mpainter-phase1}
build=${BTS_BUILD:-$root/build/linux-native}
out=${BTS_BLENDERMASKS_OUTPUT:-$root/build/blendermasks}
shots=${BTS_BLENDERMASKS_SHOTS:-$root/shots/blendermasks}
blender=${BTS_BLENDER:-/home/mlind/.local/bin/blender}
python=${BTS_PYTHON:-python3}
jobs=${BTS_JOBS:-2}
[[ "$jobs" =~ ^[1-6]$ ]] || { echo 'BTS_JOBS must be 1..6' >&2; exit 2; }
mkdir -p "$out" "$shots"
export OMP_NUM_THREADS=2 OPENBLAS_NUM_THREADS=1
unset WAYLAND_DISPLAY LD_LIBRARY_PATH QT_PLUGIN_PATH
# Keep Qt/native runtime paths away from stock Blender (different USD libraries).
native_env=(env QT_QPA_PLATFORM=xcb QT_PLUGIN_PATH="$root/toolchains/qt/plugins" LD_LIBRARY_PATH="$root/toolchains/qt/lib:$root/build/runtime/lib")
cmake --build "$build" --parallel "$jobs" --target bts-blender-presets bts-mask-generator-tests bts-mask-preset-tests bts-smart-material-preset-tests >"$out/build.log" 2>&1
for test in bts-mask-generator-tests bts-mask-preset-tests bts-smart-material-preset-tests; do
  xvfb-run -a "${native_env[@]}" "$build/bin/$test" >"$out/$test.log" 2>&1
done
xvfb-run -a "${native_env[@]}" "$build/bin/bts-blender-presets" catalog "$out/catalog" >"$out/catalog.log" 2>&1
xvfb-run -a env -u PYTHONPATH "$blender" -b -t 2 --factory-startup --python-exit-code 1 --python "$root/mpainter/scripts/blender/mpainter_presets.py" -- create "$out/catalog" "$out/Shader_Ball_JL_01-mPainter.blend" --repo "$root/mpainter" >"$out/blender-create.log" 2>&1
xvfb-run -a env -u PYTHONPATH "$blender" -b "$out/Shader_Ball_JL_01-mPainter.blend" -t 2 --python-exit-code 1 --python "$root/mpainter/scripts/blender/verify_presets.py" -- "$out/catalog" "$out/validation" >"$out/blender-test.log" 2>&1
xvfb-run -a "${native_env[@]}" "$python" "$root/mpainter/scripts/host/verify-blender-presets.py" --native "$build/bin/bts-blender-presets" --catalog "$out/catalog" --validation "$out/validation" --shots "$shots" >"$out/native-test.log" 2>&1
xvfb-run -a env -u PYTHONPATH "$blender" -b "$out/Shader_Ball_JL_01-mPainter.blend" -t 2 --python-exit-code 1 --python "$root/mpainter/scripts/blender/render_preset_comparison.py" -- "$out/catalog" "$out/validation" "$shots" >"$out/blender-render.log" 2>&1
xvfb-run -a "${native_env[@]}" "$build/bin/bts-blender-presets" import "$out/validation/grouped/blender.json" "$out/validation/grouped/roundtrip.btsmask"
cmp "$out/validation/grouped/source.btsmask" "$out/validation/grouped/roundtrip.btsmask"
"$python" "$root/mpainter/scripts/host/compare-blender-preset-renders.py" "$shots" >"$out/render-comparison.log"
printf 'PASS: Blender preset proof and images in %s\n' "$shots"
