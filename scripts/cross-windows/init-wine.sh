#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
source "$(dirname "$0")/env.sh"
# Every new wineserver session starts with `wineboot --init`, and each other Wine
# process in that session waits up to five minutes for it to finish. When the
# prefix is new, or older than Wine's wine.inf, that wineboot also installs
# wine.inf, and in this container that install never finishes (rundll32 spins).
# Every later process in the session then pays the full five minutes. Run that
# setup alone with a time limit and stop its session. Wine records the new
# timestamp before it installs, so later sessions boot in well under a second.
wine_inf=/usr/share/wine/wine.inf
stamp=$(cat "$WINEPREFIX/.update-timestamp" 2>/dev/null || true)
if [[ ! -f "$WINEPREFIX/system.reg" ||
      ( "$stamp" != disable && "$stamp" != "$(stat -c %Y "$wine_inf")" ) ]]; then
  # A running session would absorb this wineboot instead of starting a fresh one.
  wineserver -k 2>/dev/null || true
  wineserver -w
  set +e
  timeout --kill-after=5s 120s xvfb-run -a -s '-screen 0 1280x800x24 -nolisten tcp' \
    wine wineboot --init 9>&- < /dev/null
  status=$?
  set -e
  wineserver -k 2>/dev/null || true
  wineserver -w
  [[ "$status" == 0 || "$status" == 124 ]] || exit "$status"
  [[ -f "$WINEPREFIX/system.reg" ]] || { echo 'Wine prefix initialization failed' >&2; exit 1; }
  if [[ "$status" == 124 ]]; then
    echo 'Wine setup timed out after creating its prefix; the executable probe must still pass.' >&2
  fi
fi
