#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
source "$(dirname "$0")/env.sh"
repo="$(cd "$MP_WIN_SCRIPTS/../.." && pwd)"
blender="$MP_WIN_WORK/build/windows/blender"
libs="$MP_WIN_WORK/build/windows/lib-windows_x64"
# Avoid downloading Blender's several-thousand-file regression test corpus.
GIT_LFS_SKIP_SMUDGE=1 python3 "$repo/scripts/prepare-blender.py" "$blender"
git -C "$blender" -c lfs.concurrenttransfers=2 lfs pull --include='release/**'
pin=$(git -C "$blender" rev-parse HEAD:lib/windows_x64)
if [[ ! -d "$libs/.git" ]]; then
  GIT_LFS_SKIP_SMUDGE=1 git clone --depth 1 --branch blender-v5.2-release \
    https://projects.blender.org/blender/lib-windows_x64.git "$libs"
fi
[[ -z "$(git -C "$libs" status --porcelain)" ]] || { echo 'Library checkout has local edits'; exit 1; }
git -C "$libs" fetch --depth 1 origin "$pin"
GIT_LFS_SKIP_SMUDGE=1 git -C "$libs" checkout --detach "$pin"
git -C "$libs" -c lfs.concurrenttransfers=2 lfs pull --exclude='**/debug/**,**/Debug/**,**/*.pdb'
# Snapshot application sources so later local script commits do not change the input.
app="$MP_WIN_WORK/build/windows/mpainter-source"
ref=$(git -C "$repo" rev-parse "${MP_WIN_SOURCE_REF:-HEAD}^{commit}")
if [[ ! -e "$app" ]]; then
  mkdir -p "$app"
  echo "$ref" > "$MP_WIN_WORK/build/windows/source-head.txt"
  git -C "$repo" archive "$ref" | tar -x -C "$app"
elif [[ "$(cat "$MP_WIN_WORK/build/windows/source-head.txt")" != "$ref" ]]; then
  echo 'Existing source snapshot differs; use a fresh build workspace or its explicit MP_WIN_SOURCE_REF.' >&2
  exit 1
fi
