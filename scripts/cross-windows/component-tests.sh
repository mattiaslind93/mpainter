#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/env.sh"
exec 9>"$MP_WIN_WORK/build/windows/build.lock"
flock -n 9 || { echo 'Another Windows build is running.' >&2; exit 1; }
stage="$MP_WIN_WORK/build/windows/qt-smoke"
[[ -f "$stage/platforms/qoffscreen.dll" ]] || { echo 'Run qt-probe.sh first.' >&2; exit 1; }
xvfb-run -a env MP_WIN_BUILD_HEADLESS=1 cmake --build "$MP_WIN_WORK/build/windows/native" \
  --parallel 6 --target bts-branding-tests bts-theme-tests
for target in bts-branding-tests bts-theme-tests; do
  cp "$MP_WIN_WORK/build/windows/native/bin/$target.exe" "$stage/"
done
python3 "$MP_WIN_SCRIPTS/check-pe.py" "$stage" --output "$MP_WIN_WORK/logs/windows/component-dll-closure.json"
for target in bts-branding-tests bts-theme-tests; do
  "$MP_WIN_SCRIPTS/wine-headless.sh" "$stage/$target.exe" -platform offscreen
  echo "$target: PASS"
done
