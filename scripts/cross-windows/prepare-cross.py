#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Adapt only the isolated Blender build graph for a Linux host; retain app code."""
import os
from pathlib import Path
root = Path(os.environ['MP_WIN_WORK']) / 'build/windows/blender'

def replace(path, old, new):
    text = path.read_text()
    if new in text:
        return
    if text.count(old) != 1:
        raise SystemExit(f'Upstream context changed: {path}: {old}')
    path.write_text(text.replace(old, new))

platform = root / 'build_files/cmake/platform/platform_win32.cmake'
replace(platform, 'include(InstallRequiredSystemLibraries)',
        'if(CMAKE_HOST_WIN32)\n  include(InstallRequiredSystemLibraries)\nendif()')
# Generator scripts execute on Linux; the embedded Python library remains Windows.
replace(platform,
        'set(PYTHON_EXECUTABLE ${LIBDIR}/python/${_PYTHON_VERSION_NO_DOTS}/bin/python$<$<CONFIG:Debug>:_d>.exe)',
        'find_program(PYTHON_EXECUTABLE_NATIVE python3 REQUIRED)\nset(PYTHON_EXECUTABLE ${PYTHON_EXECUTABLE_NATIVE})')
manifest = root / 'release/windows/manifest/blender.manifest.in'
if not manifest.exists():
    manifest.symlink_to('Blender.manifest.in')

# These custom commands use TARGET_FILE rather than the target name, so CMake
# does not automatically insert CMAKE_CROSSCOMPILING_EMULATOR.
runner = '"$ENV{MP_WIN_SCRIPTS}/wine-tool.py"'
for relative, target in (
    ('source/blender/makesdna/intern/CMakeLists.txt', 'makesdna'),
    ('source/blender/makesrna/intern/CMakeLists.txt', 'makesrna'),
):
    path = root / relative
    old = f'    "$<TARGET_FILE:{target}>"'
    replace(path, old, f'    {runner} "$<TARGET_FILE:{target}>"')
macros = root / 'build_files/cmake/macros.cmake'
text = macros.read_text()
for target in ('datatoc', 'shader_tool'):
    text = text.replace(f'COMMAND "$<TARGET_FILE:{target}>"',
                        f'COMMAND {runner} "$<TARGET_FILE:{target}>"')
if text != macros.read_text():
    macros.write_text(text)
replace(macros, 'env ${PLATFORM_ENV_BUILD} "$<TARGET_FILE:msgfmt>"',
        f'env ${{PLATFORM_ENV_BUILD}} {runner} "$<TARGET_FILE:msgfmt>"')
replace(root / 'intern/cycles/kernel/osl/shaders/CMakeLists.txt',
        '${OSL_COMPILER} -q -O2', f'{runner} ${{OSL_COMPILER}} -q -O2')
replace(platform, 'set(PLATFORM_ENV_BUILD "PATH=${PLATFORM_ENV_BUILD_DIRS}")',
        'set(PLATFORM_ENV_BUILD "MP_WIN_BUILD_HEADLESS=1")')

# Preserve Blender's explicit scalable allocations, but do not inject its
# process-wide CRT detours into the Qt host before main() runs.
replace(root / 'source/creator/creator.cc',
        '#if defined(WITH_TBB_MALLOC) && defined(_MSC_VER) && defined(NDEBUG)\n',
        '#if defined(WITH_TBB_MALLOC) && defined(_MSC_VER) && defined(NDEBUG) && !defined(WITH_BTS_HOST)\n')
