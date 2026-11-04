#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/env.sh"
# An isolated prefix and Xvfb keep even Wine error dialogs off the desktop.
exec timeout --kill-after=10s 180s xvfb-run -a -s '-screen 0 1280x800x24 -nolisten tcp' wine "$@"
