#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Add aliases for Windows library paths on a case-sensitive Linux filesystem."""
import os
import re
from pathlib import Path
root = Path(os.environ['MP_WIN_WORK']) / 'build/windows/lib-windows_x64'
# Do not recurse into aliases, and do not change the versioned library files.
for directory, dirs, files in os.walk(root, followlinks=False):
    dirs[:] = [d for d in dirs if d != '.git' and not (Path(directory)/d).is_symlink()]
    for name in dirs + files:
        path = Path(directory) / name.lower()
        if not path.exists() and not path.is_symlink():
            path.symlink_to(name)
for name in ('Config.cmake', 'ConfigVersion.cmake'):
    path = root / 'imath/lib/cmake/Imath' / ('IMATH' + name)
    if not path.exists():
        path.symlink_to('Imath' + name)

# Imported CMake targets sometimes spell DLLs differently from their Git names.
def alias_path(base, suffix):
    for part in Path(suffix).parts:
        wanted = base / part
        if not wanted.exists() and base.is_dir():
            matches = [p for p in base.iterdir() if p.name.casefold() == part.casefold() and not p.is_symlink()]
            if len(matches) == 1 and not wanted.is_symlink():
                wanted.symlink_to(matches[0].name)
        base = wanted

for path in root.rglob('*.cmake'):
    if path.is_symlink():
        continue
    content = path.read_text(errors='replace')
    package = root / path.relative_to(root).parts[0]
    for var, suffix in re.findall(r'"\$\{(_IMPORT_PREFIX|CMAKE_CURRENT_LIST_DIR)\}/([^";]+)"', content):
        if '$' not in suffix:
            alias_path(package if var == '_IMPORT_PREFIX' else path.parent, suffix)

# Upstream's installed OIIO config embeds Windows absolute paths. CMake running
# on Linux cannot pass these to file(RELATIVE_PATH); both paths share a prefix.
oiio = root / 'OpenImageIO/lib/cmake/OpenImageIO/OpenImageIOConfig.cmake'
original = oiio.read_text()
content = original.replace('C:/db/build/S/VS1564R/Release/openimageio/', str(root / 'OpenImageIO') + '/')
if content != original:
    oiio.write_text(content)

# Blender also uses mixed case in hardcoded LIBDIR paths. Resolve the actual
# generated Ninja paths against the checkout, without rewriting the build file.
ninja = root.parent / 'native/build.ninja'
if ninja.exists():
    build_text = ninja.read_text()
    # Runtime DLL spellings also occur only in generated install scripts.
    library_paths = build_text + '\n' + '\n'.join(
        path.read_text() for path in ninja.parent.rglob('cmake_install.cmake'))
    for suffix in set(re.findall(r'/lib-windows_x64/([^\s";<>$)]+)', library_paths)):
        alias_path(root, suffix)
    sdk = Path(os.environ['MP_WIN_SDK'])
    for name in set(re.findall(r'(?<![\w/])([A-Za-z0-9_]+\.lib)\b', build_text)):
        for libdir in (sdk / 'crt/lib/x86_64', sdk / 'sdk/lib/ucrt/x86_64', sdk / 'sdk/lib/um/x86_64'):
            alias_path(libdir, name)

# Blender's Win32 GHOST code spells these SDK headers with mixed case.
sdk = Path(os.environ['MP_WIN_SDK'])
for name in ('D3D11.h', 'Dwmapi.h'):
    alias_path(sdk / 'sdk/include/um', name)
