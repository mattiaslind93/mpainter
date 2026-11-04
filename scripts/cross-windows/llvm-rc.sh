#!/usr/bin/env bash
set -euo pipefail
# Match the default Windows-1252 resource input used by these dependencies.
exec llvm-rc /C 1252 "$@"
