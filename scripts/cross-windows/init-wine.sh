#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/env.sh"
# Wine's first prefix setup can stall in container services. Complete the files
# first, then stop only this experiment's wineserver before testing the prefix.
if [[ ! -f "$WINEPREFIX/system.reg" ]]; then
  set +e
  timeout --kill-after=5s 120s xvfb-run -a wineboot --init
  status=$?
  set -e
  wineserver -k || true
  [[ "$status" == 0 || "$status" == 124 ]] || exit "$status"
  [[ -f "$WINEPREFIX/system.reg" ]] || { echo 'Wine prefix initialization failed' >&2; exit 1; }
  if [[ "$status" == 124 ]]; then
    echo 'Wine setup timed out after creating its prefix; the executable probe must still pass.' >&2
  fi
fi
