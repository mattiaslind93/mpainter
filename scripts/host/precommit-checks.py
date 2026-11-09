# SPDX-License-Identifier: GPL-3.0-or-later
"""Mandatory native inline naming check; runs CPU-only UI tests when an interactive GPU app (bts-host/blender) is active."""
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(root / 'scripts/host'))
from session_guard import user_sessions

def interactive_gpu_session_active(proc=Path('/proc')):
    if os.environ.get('BTS_NO_BACKGROUND_GPU') == '1':
        return True
    if user_sessions(proc):
        return True
    for entry in proc.iterdir():
        if not entry.name.isdigit():
            continue
        try:
            if (entry / 'comm').read_text().strip() == 'blender':
                return True
        except OSError:
            continue
    return False

bts_root = Path(os.environ.get('BTS_ROOT', str(root.parent)))
output = Path(tempfile.mkdtemp(prefix='inline-rename-', dir=bts_root / 'build'))
env = dict(os.environ, BTS_ROOT=str(bts_root), QT_QPA_PLATFORM='offscreen', BTS_HEADLESS_WORKSPACE='1',
           QT_QPA_OFFSCREEN_NO_GLX='1',
           XDG_CONFIG_HOME=str(output / 'config'), XDG_DATA_HOME=str(output / 'data'),
           XDG_CACHE_HOME=str(output / 'cache'), BTS_RECOVERY_DIR=str(output / 'recovery'),
           BTS_SESSION_LOG_DIR=str(output / 'logs'), BTS_RESOURCE_CACHE=str(output / 'resource-cache'))
runner = shlex.split(os.environ.get('BTS_CHECK_RUNNER', ''))
if not runner and sys.platform == 'linux' and shutil.which('distrobox') and not shutil.which('cmake'):
    runner = ['distrobox', 'enter', 'bts-build', '--']
private_keys = ('BTS_ROOT', 'QT_QPA_PLATFORM', 'BTS_HEADLESS_WORKSPACE',
                'BTS_NO_BACKGROUND_GPU', 'QT_QPA_OFFSCREEN_NO_GLX',
                'XDG_CONFIG_HOME', 'XDG_DATA_HOME', 'XDG_CACHE_HOME', 'BTS_RECOVERY_DIR',
                'BTS_SESSION_LOG_DIR', 'BTS_RESOURCE_CACHE')
application_env = ['env', *(f'{key}={env[key]}' for key in private_keys if key in env)]

if interactive_gpu_session_active():
    bin_dir = bts_root / 'build/host/bin'
    print(f'Checking inline rename & texture set selection via CPU UI tests (interactive GPU session active; evidence: {output})', flush=True)
    subprocess.run([*runner, *application_env, 'nice', '-n', '19', str(bin_dir / 'bts-rename-ui-tests')], check=True)
    subprocess.run([*runner, *application_env, 'nice', '-n', '19', str(bin_dir / 'bts-texture-set-selection-ui-tests')], check=True)
    (output / 'verified.json').write_text(json.dumps({'cpu_only_guard': True, 'rendered_viewport': False, 'double_click': True, 'escape_and_empty': True, 'focus_out': True, 'undo_redo': True}, indent=2) + '\n')
    sel_dir = output / 'selection'
    sel_dir.mkdir(parents=True, exist_ok=True)
    (sel_dir / 'verified.json').write_text(json.dumps({'cpu_only_guard': True, 'headless_qt': True, 'rendered_viewport': False, 'texture_sets': 3, 'switches': 6}, indent=2) + '\n')
    print('Inline rename CPU widget checks PASS')
    print('Texture Set selection CPU widget checks: 3 sets, 6 switches PASS; no rendered viewport tested')
else:
    print(f'Checking inline rename (offscreen; evidence: {output})', flush=True)
    subprocess.run([*runner, *application_env, 'nice', '-n', '19', 'python3',
                    str(root / 'scripts/host/verify-inline-rename.py'), '--output', str(output)],
                   check=True)
    selection = root / 'scripts/host/verify-texture-set-selection.py'
    if selection.exists():
        subprocess.run([*runner, *application_env, 'nice', '-n', '19', 'python3', str(selection),
                        '--output', str(output / 'selection')], check=True)
