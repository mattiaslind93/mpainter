#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
source "$(dirname "$0")/env.sh"
stage="${1:?Pass the staged Windows runtime directory}"
[[ -f "$stage/mPainter.exe" ]] || { echo 'No application executable to capture.' >&2; exit 1; }
capture="$MP_WIN_WORK/logs/windows2/wine-capture"
mkdir -p "$capture"
# Qt writes Windows paths; Wine maps the container filesystem through Z:.
export BTS_BRANDING_CAPTURE="Z:$capture" BTS_BRANDING_QUIT=1
export __GLX_VENDOR_LIBRARY_NAME=mesa GALLIUM_DRIVER=llvmpipe
export MESA_LOADER_DRIVER_OVERRIDE=llvmpipe QT_OPENGL=desktop
# Wine's graphics enumeration can touch Vulkan even for an OpenGL window.
# Select Mesa's software ICD, not a host hardware device, when available.
if [[ -f /usr/share/vulkan/icd.d/lvp_icd.x86_64.json ]]; then
  export VK_DRIVER_FILES=/usr/share/vulkan/icd.d/lvp_icd.x86_64.json
  export VK_ICD_FILENAMES="$VK_DRIVER_FILES"
fi
unset BTS_NO_BACKGROUND_GPU WINEPATH
# Wine 11's EGL device path conflicts with forced software rendering. Use
# GLX for this executable in the isolated test prefix; no host registry changes.
"$MP_WIN_SCRIPTS/wine-headless.sh" reg add 'HKCU\Software\Wine\AppDefaults\mPainter.exe\X11 Driver' /v UseEGL /t REG_SZ /d N /f
# Explicit windows QPA overrides the offscreen default; Wine still renders only
# inside wine-headless.sh's private, software-rendered Xvfb display.
"$MP_WIN_SCRIPTS/wine-headless.sh" "$stage/mPainter.exe" -platform windows
[[ -s "$capture/workspace.png" && -s "$capture/branding.json" ]]
cp "$capture/workspace.png" "$MP_WIN_WORK/win.png"
echo "Main-window capture: $MP_WIN_WORK/win.png"
