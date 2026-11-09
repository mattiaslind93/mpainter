#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
source "$(dirname "$0")/env.sh"
stage="${1:?Pass the staged Windows runtime directory}"
[[ -f "$stage/mPainter.exe" ]] || { echo 'No application executable to test.' >&2; exit 1; }
python3 "$MP_WIN_SCRIPTS/check-pe.py" "$stage" --allow-optional-gpu-drivers --output "$MP_WIN_WORK/logs/windows/app-dll-closure.json"
# The actual application has no --self-test; these existing options exercise
# executable startup and CPU-only Qt workspace lifetime without GPU initialization.
"$MP_WIN_SCRIPTS/wine-headless.sh" "$stage/mPainter.exe" --version
"$MP_WIN_SCRIPTS/wine-headless.sh" "$stage/mPainter.exe" -platform offscreen --cpu-workspace-shutdown-test
