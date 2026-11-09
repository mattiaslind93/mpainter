#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Run one Windows generator under the build's private Xvfb display."""
import os
from pathlib import Path
import subprocess
import sys

if os.environ.get('MP_WIN_BUILD_HEADLESS') != '1' or not os.environ.get('DISPLAY'):
    raise SystemExit('Windows generators require the private xvfb-run build wrapper')
root = Path(os.environ['MP_WIN_WORK'])
env = os.environ.copy()
# Expose only downloaded target libraries and locally built target dependencies.
dirs = [root / 'toolchains/windows/redist', root / 'toolchains/windows/deps/bin']
dirs += sorted((root / 'build/windows/lib-windows_x64').glob('*/bin'))
dirs += [root / 'build/windows/lib-windows_x64/python/313/bin']
env['WINEPATH'] = ';'.join('Z:' + str(p) for p in dirs if p.is_dir())
exe, *args = sys.argv[1:]
def winpath(arg):
    if arg.startswith(('/mnt/', '/tmp/', '/home/', '/usr/', '/run/')):
        return 'Z:' + arg
    if arg.startswith('-I/'):
        return '-IZ:' + arg[2:]
    return arg
result = subprocess.run(['wine', exe, *map(winpath, args)], env=env)
# Generated includes and depfiles must describe paths understood by host Clang
# and Ninja. Only rewrite generated build outputs, never source inputs.
if result.returncode == 0:
    generated = set()
    for arg in args:
        p = Path(arg)
        if p.is_relative_to(root / 'build/windows/native'):
            if p.is_dir():
                generated.update(p.iterdir())
            else:
                generated.add(p)
    for p in generated:
        if p.suffix in {'.d', '.cc', '.c', '.h', '.hh', '.info'} and p.is_file():
            text = p.read_text()
            normalized = text.replace('Z:/', '/').replace('z:/', '/')
            if normalized != text:
                p.write_text(normalized)
raise SystemExit(result.returncode)
