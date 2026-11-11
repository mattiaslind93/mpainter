#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Invoke upstream's updater without changing the user's global Git configuration.
set -euo pipefail
source_dir=$(cd "${1:?Blender source directory required}" && pwd)
state_dir=${2:?An isolated state directory is required}
mkdir -p "$state_dir"
state_dir=$(cd "$state_dir" && pwd)
export GIT_CONFIG_GLOBAL="$state_dir/gitconfig"
export GIT_CONFIG_NOSYSTEM=1
export GIT_TERMINAL_PROMPT=0
# LFS already runs in the source repo, but its default install scope is global.
# The explicit global file above confines those filter settings to this build.
git lfs install --skip-repo
cd "$source_dir"
python3 build_files/utils/make_update.py --no-blender
# --no-blender also skips the source repository's LFS payloads (startup.blend).
git lfs pull
