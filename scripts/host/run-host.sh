#!/usr/bin/env bash
set -euo pipefail
export BTS_ROOT=${BTS_ROOT:-$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../../.." && pwd -P)}
project_dir="$(cd "$(dirname "$0")/../.." && pwd)"
workspace_dir="$(cd "$project_dir/.." && pwd)"
bts_test_run=false
bts_headless_run=false
for bts_arg in "$@"; do
  case "$bts_arg" in --cpu-*|--headless-*|--prepare-native-craftypack|--prepare-native-craftypack=*) bts_headless_run=true; bts_test_run=true ;; --*demo*|--screenshot|--state-json|--quit-after|--exit-after) bts_test_run=true ;; esac
done
if [[ -n "${BTS_IDLE_DEMO_DIR:-}" ]]; then bts_headless_run=true; bts_test_run=true; fi
export BTS_CPU_TEST_ONLY=$(python3 -c 'import sys;sys.path.insert(0,sys.argv[1]);from session_guard import cpu_test_arguments;print(int(cpu_test_arguments(sys.argv[2:])))' "$project_dir/scripts/host" "$@")
if [[ "$BTS_CPU_TEST_ONLY" == 1 ]]; then
  export QT_QPA_PLATFORM=offscreen QT_QPA_OFFSCREEN_NO_GLX=1
elif "$bts_test_run" || [[ "${BTS_NO_BACKGROUND_GPU:-0}" == 1 ]]; then
  python3 -c 'import os,sys;sys.path.insert(0,sys.argv[1]);from session_guard import require_gui_allowed;require_gui_allowed(allow_private_xvfb=True,caller_pid=os.getppid())' "$project_dir/scripts/host"
fi
if "$bts_headless_run"; then export QT_QPA_PLATFORM=${QT_QPA_PLATFORM:-offscreen}; fi
if "$bts_test_run" && ! "$bts_headless_run" && [[ "${QT_QPA_PLATFORM:-xcb}" != offscreen && "${QT_QPA_PLATFORM:-xcb}" != minimal ]]; then
  python3 -c 'import os,sys;sys.path.insert(0,sys.argv[1]);from session_guard import require_gui_allowed;require_gui_allowed(allow_private_xvfb=True,caller_pid=os.getppid())' "$project_dir/scripts/host"
fi
if "$bts_test_run"; then
  bts_test_dir="$BTS_ROOT/build/session-tests/$$"
  case "${XDG_CONFIG_HOME:-}" in "$BTS_ROOT"/build/*/config|"$BTS_ROOT"/build/*/.config) ;; *) export XDG_CONFIG_HOME="$bts_test_dir/config" ;; esac
  case "${BTS_RECOVERY_DIR:-}" in "$BTS_ROOT"/build/*) ;; *) export BTS_RECOVERY_DIR="$bts_test_dir/recovery" ;; esac
  export XDG_DATA_HOME="${XDG_DATA_HOME:-$bts_test_dir/data}"
fi
if [[ "$BTS_CPU_TEST_ONLY" != 1 ]]; then source "$project_dir/scripts/host/gpu-env.sh"; fi
export BTS_RECOVERY_DIR=${BTS_RECOVERY_DIR:-"${XDG_DATA_HOME:-$HOME/.local/share}/mPainter/recovery"}
if [[ "${BTS_NO_BACKGROUND_GPU:-0}" != 1 && "$bts_test_run" == false ]]; then export QT_QPA_PLATFORM=xcb; else export QT_QPA_PLATFORM=${QT_QPA_PLATFORM:-xcb}; fi
export __GL_SYNC_TO_VBLANK=0
qt_sdk="${QT_ROOT_DIR:-$workspace_dir/toolchains/qt}"
export QT_PLUGIN_PATH="$qt_sdk/plugins"
export LD_LIBRARY_PATH="$qt_sdk/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
# Resolve this Linux installation's approved resource; the native core validates its hash.
# Installations with a different layout can supply BTS_ENVIRONMENT or --environment.
if [[ -z "${BTS_ENVIRONMENT:-}" ]]; then
  export BTS_ENVIRONMENT="$workspace_dir/build/runtime/5.2/datafiles/studiolights/world/default-studio.hdr"
fi
if ! "$bts_test_run"; then
  export BTS_PERFORMANCE_PROFILE=1 BTS_SESSION_PROFILE=1
fi
# Process substitution keeps the application PID and exit status; the helper
# timestamps each line and tees it to the original output and the session file.
exec > >(python3 -u "$project_dir/scripts/host/session_logging.py" --directory "${BTS_SESSION_LOG_DIR:-$BTS_ROOT/logs}") 2>&1
exec "${BTS_HOST_EXECUTABLE:-$workspace_dir/build/runtime/mpainter}" "$@"
