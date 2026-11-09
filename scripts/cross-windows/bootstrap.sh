#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
source "$(dirname "$0")/env.sh"
# This intentionally changes only the container. Package cache is on the work disk.
sudo dnf --setopt="cachedir=$MP_WIN_WORK/cache/windows/dnf" install -y \
  clang clang-tools-extra lld llvm wine-core python3-pip cabextract msitools \
  cmake ninja-build git-lfs xorg-x11-server-Xvfb xorg-x11-xauth
cd "$MP_WIN_WORK/cache/windows"
curl -LfsS --retry 2 --max-time 240 \
  https://github.com/Jake-Shadle/xwin/releases/download/0.10.0/xwin-0.10.0-x86_64-unknown-linux-musl.tar.gz -o xwin.tar.gz
echo 'd870eb4b2f390878af6da1ccd3cf321d22fcb72720984853b4be732ae597fc88  xwin.tar.gz' | sha256sum -c -
mkdir -p "$MP_WIN_WORK/toolchains/windows"
tar -xzf xwin.tar.gz -C "$MP_WIN_WORK/toolchains/windows"
python3 "$MP_WIN_SCRIPTS/fetch-manifest.py"
# See README.md for the proprietary SDK/CRT terms; xwin's own MIT/Apache
# license does not apply to Microsoft payloads. Nothing here is redistributed.
"$MP_WIN_WORK/toolchains/windows/xwin-0.10.0-x86_64-unknown-linux-musl/xwin" \
  --manifest "$MP_WIN_SCRIPTS/msvc-channel.json" \
  --accept-license --arch x86_64 \
  --cache-dir "$MP_WIN_WORK/cache/windows/xwin" --http-retry 2 splat --output "$MP_WIN_SDK"
python3 -m venv "$MP_WIN_WORK/toolchains/windows/venv"
"$MP_WIN_WORK/toolchains/windows/venv/bin/pip" install aqtinstall==3.3.0
"$MP_WIN_WORK/toolchains/windows/venv/bin/aqt" install-qt windows desktop 6.8.3 win64_msvc2022_64 \
  -O "$MP_WIN_WORK/toolchains/windows/qt"
curl -LfsS --retry 2 --max-time 240 https://github.com/assimp/assimp/archive/refs/tags/v5.4.3.tar.gz -o assimp-5.4.3.tar.gz
curl -LfsS --retry 2 --max-time 240 https://github.com/HDFGroup/hdf5/archive/refs/tags/hdf5_1.14.6.tar.gz -o hdf5-1.14.6.tar.gz
curl -LfsS --retry 2 --max-time 240 https://github.com/lz4/lz4/archive/refs/tags/v1.10.0.tar.gz -o lz4-1.10.0.tar.gz
python3 "$MP_WIN_SCRIPTS/fetch-crt.py"
