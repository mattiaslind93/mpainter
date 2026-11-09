#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
# Match the default Windows-1252 resource input used by these dependencies.
exec llvm-rc /C 1252 "$@"
