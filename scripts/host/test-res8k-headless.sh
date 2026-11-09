#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
# Invoke inside distrobox bts-build. One private X server and one 128px project.
root=${BTS_ROOT:-/mnt/mpainter-phase1}
repo="$root/mpainter"
out="$root/build/res8k-proof"
deliver=${BTS_RES8K_DELIVERABLES:-$root}
mkdir -p "$out" "$deliver/shots/dropdowns" "$root/build/mpainter-test-sessions"
unset WAYLAND_DISPLAY
export QT_QPA_PLATFORM=xcb QT_PLUGIN_PATH="$root/toolchains/qt/plugins"
export LD_LIBRARY_PATH="$root/toolchains/qt/lib:$root/build/runtime/lib"
export OMP_NUM_THREADS=4 OPENBLAS_NUM_THREADS=1
export BTS_ROOT="$root" BTS_HOST_EXECUTABLE="${BTS_HOST_EXECUTABLE:-$root/build/linux-native/bin/mpainter}"
export BTS_RES8K_DEMO=1 BTS_HOSTED_SHADER_WORKERS=0 BTS_PERFORMANCE_PROFILE=1
export XDG_CONFIG_HOME="$root/build/mpainter-test-sessions/res8k/config"
# Unique isolated library: repeated proof runs must not accumulate fixture cards.
proof_session=$(mktemp -d "$root/build/mpainter-test-sessions/res8k-data.XXXXXX")
export XDG_DATA_HOME="$proof_session" BTS_RECOVERY_DIR="$proof_session/recovery"
BTS_DROPDOWN_SCREENSHOT="$out/ordinary-dropdown.png" BTS_CHECKBOX_SCREENSHOT="$out/checkboxes.png" \
  xvfb-run -a -s '-screen 0 1600x1100x24' "$root/build/linux-native/bin/bts-theme-tests"
if xvfb-run -a "$BTS_HOST_EXECUTABLE" --no-splash --gui-fix-resolution 16384 > "$out/cli-rejection.log" 2>&1; then
  echo 'Oversized CLI resolution was accepted' >&2
  exit 1
else
  test "$?" -eq 2
fi
grep -q '128 to 8192 (8K)' "$out/cli-rejection.log"
source "$repo/scripts/host/gpu-env.sh"
export BTS_ENVIRONMENT="$root/build/runtime/5.2/datafiles/studiolights/world/default-studio.hdr"
xvfb-run -a -s '-screen 0 1600x1100x24' bash -c 'export BTS_PRIVATE_XVFB_PID=$(cat "/tmp/.X${DISPLAY#:}-lock"); exec "$BTS_HOST_EXECUTABLE" "$@"' res8k-test --workspace --gui-fix-demo "$out" --gui-fix-mesh "$repo/docs/checkbox/residency.obj" --gui-fix-resolution 128
cp "$out/res8k.png" "$deliver/res8k.png"
cp "$out/dropdowns/"*.png "$deliver/shots/dropdowns/"
cp "$out/shelf-materials.png" "$out/shelf-material-presets.png" "$deliver/shots/"
cp "$out/res8k-proof.json" "$deliver/shots/res8k-proof.json"
