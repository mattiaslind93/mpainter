#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Build the windowless embedded Blender/Metal diagnostic, never the editor."""
import argparse
import os
import pathlib
import platform
import subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--blender', type=pathlib.Path, required=True)
p.add_argument('--build', type=pathlib.Path, required=True)
p.add_argument('--qt', type=pathlib.Path, required=True)
p.add_argument('--jobs', type=int, default=6, choices=range(1, 7))
a = p.parse_args()
if platform.system() != 'Darwin' or platform.machine() != 'arm64':
    p.error('This diagnostic requires macOS arm64')
# Descendants inherit low priority, including Ninja and compiler processes.
priority = os.getpriority(os.PRIO_PROCESS, 0)
if priority < 15:
    os.nice(15 - priority)
root = pathlib.Path(__file__).resolve().parents[2]
subprocess.run(['python3', str(root / 'scripts/prepare-blender.py'), str(a.blender)], check=True)
subprocess.run(['python3', str(root / 'scripts/macos/apply-headless-patch.py'), str(a.blender)], check=True)
args = ['cmake', '-S', str(a.blender.resolve()), '-B', str(a.build.resolve()), '-G', 'Ninja',
        '-DCMAKE_BUILD_TYPE=Release', '-DCMAKE_OSX_ARCHITECTURES=arm64',
        '-DCMAKE_PREFIX_PATH=' + str(a.qt.resolve()),
        '-DQt6_DIR=' + str(a.qt.resolve() / 'lib/cmake/Qt6'),
        '-DBTS_HOST_SOURCE_DIR=' + str(root / 'native/host'),
        '-DBTS_MACOS_METAL_PROBE=ON', '-DWITH_HEADLESS=ON',
        '-DWITH_METAL_BACKEND=ON', '-DWITH_OPENGL_BACKEND=OFF',
        '-DWITH_VULKAN_BACKEND=OFF', '-DWITH_INPUT_NDOF=OFF',
        '-DWITH_COMPILER_CCACHE=OFF', '-DWITH_BUILDINFO=OFF', '-DWITH_GTESTS=OFF',
        '-DWITH_CYCLES_DEVICE_CUDA=OFF', '-DWITH_CYCLES_DEVICE_OPTIX=OFF',
        '-DWITH_CYCLES_DEVICE_HIP=OFF', '-DWITH_CYCLES_DEVICE_ONEAPI=OFF',
        '-DWITH_CYCLES_DEVICE_METAL=ON', '-DWITH_BLENDER_THUMBNAILER=OFF',
        '-DCMAKE_INSTALL_PREFIX=' + str(a.build.resolve() / 'runtime')]
subprocess.run(args, check=True)
# The install target builds auxiliary runtime libraries too (e.g. the Draco
# bridge), which are not dependencies of the Blender executable itself.
subprocess.run(['cmake', '--build', str(a.build.resolve()), '--parallel', str(a.jobs),
                '--target', 'install'], check=True)
