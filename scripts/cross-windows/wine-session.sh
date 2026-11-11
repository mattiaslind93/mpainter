#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Run one build command inside a single warm, persistent Wine session:
#
#   xvfb-run ... env MP_WIN_BUILD_HEADLESS=1 bash wine-session.sh cmake --build ...
#
# Source env.sh first and hold build.lock; this stops any other session on the
# prefix. The Windows generator steps (wine-tool.py) then attach to a server whose
# wineboot has already finished. The session's own processes (wineboot, services,
# explorer) start here, with their output going to a log and without the build
# lock, so no Wine process keeps a Ninja output pipe or build.lock open after
# its step.
set -euo pipefail
[[ "${MP_WIN_BUILD_HEADLESS:-}" == 1 && -n "${DISPLAY:-}" ]] ||
  { echo 'Run inside the private xvfb-run build wrapper' >&2; exit 1; }
: "${WINEPREFIX:?Source env.sh first}" "${MP_WIN_SCRIPTS:?Source env.sh first}"
log=$MP_WIN_WORK/logs/windows/wine-session.log
# A server left by an interrupted build may still be waiting on an unfinished wineboot.
wineserver -k 2>/dev/null || true
wineserver -w
bash "$MP_WIN_SCRIPTS/init-wine.sh"
trap 'wineserver -k 2>/dev/null || true' EXIT
trap 'exit 143' TERM INT HUP
wineserver -p 9>&- < /dev/null >> "$log" 2>&1
# A healthy boot takes well under a second. Fail here instead of letting every
# generator step wait out Wine's five-minute boot timeout.
timeout --kill-after=5s 60s wine cmd.exe /c exit 9>&- < /dev/null >> "$log" 2>&1 ||
  { echo "The Wine session did not boot; see $log" >&2; exit 1; }
"$@"
