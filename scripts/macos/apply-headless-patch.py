#!/usr/bin/env python3
"""Apply the narrow Mac CLI patches after the pinned host integration patch."""
import pathlib
import subprocess
import sys
root = pathlib.Path(__file__).resolve().parents[2]
source = pathlib.Path(sys.argv[1]).resolve()
command = ['git', '-C', str(source), 'apply']
for name in ('0002-macos-headless-errors.patch', '0003-macos-isolated-cache.patch'):
    patch = root / 'third_party/blender/patches' / name
    if subprocess.run(command + ['--reverse', '--check', str(patch)], capture_output=True).returncode:
        subprocess.run(command + ['--check', str(patch)], check=True)
        subprocess.run(command + [str(patch)], check=True)
print('macOS CLI patches ready: non-modal headless errors and explicit cache override.')
