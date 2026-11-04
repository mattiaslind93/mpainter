#!/bin/bash
set -euo pipefail
# Reserved for a host with validated Metal presentation; do not ship an OpenGL-only app.
if [[ ${MPAINTER_MACOS_VIEWPORT_VALIDATED:-0} != 1 ]]; then
  echo 'macOS packaging requires a validated Metal viewport port.' >&2
  exit 1
fi
runtime=$1
output=$2
mkdir -p "$output"
cp -R "$runtime/mPainter.app" "$output/mPainter.app"
macdeployqt "$output/mPainter.app" -always-overwrite
codesign --force --deep --sign - "$output/mPainter.app"
hdiutil create -volname mPainter -srcfolder "$output/mPainter.app" -ov -format UDZO "$output/mPainter-1.0.0-macos-arm64.dmg"
