#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Run the diagnostic and existing regressions without any visible window."""
import argparse
import json
import os
from pathlib import Path
import subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--build', type=Path, required=True)
p.add_argument('--output', type=Path, required=True,
               help='New directory; existing evidence is never overwritten')
a = p.parse_args()
root = Path(__file__).resolve().parents[2]
build = a.build.resolve()
output = a.output.resolve()
output.mkdir(parents=True, exist_ok=False)
priority = os.getpriority(os.PRIO_PROCESS, 0)
if priority < 15:
    os.nice(15 - priority)
runtime = build / 'runtime/Blender.app/Contents/Resources'
env = os.environ.copy()
for name, directory in {
    'XDG_CONFIG_HOME': 'config', 'XDG_DATA_HOME': 'data', 'XDG_CACHE_HOME': 'cache',
    'BLENDER_USER_RESOURCES': 'blender-user', 'BLENDER_USER_CONFIG': 'blender-config',
    'BLENDER_USER_SCRIPTS': 'blender-scripts', 'BLENDER_USER_DATAFILES': 'blender-data',
    'BLENDER_USER_EXTENSIONS': 'blender-extensions', 'MPAINTER_CACHE_DIR': 'blender-cache',
    'BTS_RECOVERY_DIR': 'recovery', 'TMPDIR': 'tmp', 'CFFIXED_USER_HOME': 'foundation-home',
}.items():
    path = output / directory
    path.mkdir()
    env[name] = str(path)
env.update(QT_QPA_PLATFORM='offscreen', QT_QPA_OFFSCREEN_NO_GLX='1',
           OMP_NUM_THREADS='6', OPENBLAS_NUM_THREADS='6',
           MPAINTER_SMOKE_DIR=str(output),
           BLENDER_SYSTEM_RESOURCES=str(runtime / '5.2'),
           BLENDER_SYSTEM_SCRIPTS=str(runtime / '5.2/scripts'),
           BLENDER_SYSTEM_DATAFILES=str(runtime / '5.2/datafiles'),
           BLENDER_SYSTEM_PYTHON=str(runtime / '5.2/python'),
           BTS_CHECKBOX_SCREENSHOT=str(output / 'qt-theme.png'))
tests = {
    'bvh-cpu': [str(build / 'bin/mpainter-macos-bvh-cpu')],
    'erase-cpu': [str(build / 'bin/mpainter-macos-erase-cpu')],
    'storage-cpu': [str(build / 'bin/mpainter-macos-storage-cpu')],
    'theme-cpu': [str(build / 'bin/mpainter-macos-theme-cpu')],
    'mask-cpu': [str(build / 'bin/mpainter-macos-mask-cpu')],
    'export-cpu': [str(build / 'bin/mpainter-macos-export-cpu')],
    'save-safety-cpu': [str(build / 'bin/mpainter-macos-save-safety-cpu')],
    'metal-probe': [str(build / 'bin/mpainter-metal-probe'), '--self-test', str(output)],
    'cycles-cpu': [str(build / 'runtime/Blender.app/Contents/MacOS/Blender'),
                   '--background', '--factory-startup', '--disable-autoexec', '--threads', '6',
                   '--python-exit-code', '1', '--python', str(root / 'scripts/macos/cycles-smoke.py')],
}
tests['cycles-metal'] = tests['cycles-cpu'][:]
results = {}
for name, command in tests.items():
    with (output / (name + '.log')).open('w') as log:
        try:
            test_env = env.copy()
            test_env['MPAINTER_CYCLES_BACKEND'] = 'METAL' if name == 'cycles-metal' else 'CPU'
            completed = subprocess.run(command, env=test_env, cwd=output, stdout=log,
                                       stderr=subprocess.STDOUT, timeout=300)
            results[name] = {'exit_code': completed.returncode}
        except (OSError, subprocess.TimeoutExpired) as error:
            results[name] = {'error': str(error)}
    print(name, results[name], flush=True)
(output / 'results.json').write_text(json.dumps(results, indent=2) + '\n')
raise SystemExit(0 if all(r.get('exit_code') == 0 for r in results.values()) else 1)
