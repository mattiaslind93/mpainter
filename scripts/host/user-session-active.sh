#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -eu
script_dir="$(cd "$(dirname "$0")" && pwd)"
exec python3 "$script_dir/session_guard.py" "$@"
