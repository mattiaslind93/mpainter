# SPDX-License-Identifier: GPL-3.0-or-later
"""Exercise cache creation and eye release on an owned GPU display.

Respects the existing no-background-GPU and active-session guards. Run only
when a GPU proof is authorized; builds continue to use BTS_NO_BACKGROUND_GPU=1.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
from private_gpu_gui import private_gpu_gui

parser = argparse.ArgumentParser()
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--binary', type=Path, required=True)
parser.add_argument('--mesh', type=Path, required=True)
parser.add_argument('--environment', type=Path, required=True)
parser.add_argument('--verify-gpu-stack', action='store_true')
args = parser.parse_args()
root = Path(__file__).resolve().parents[2]
out = args.output.resolve()
env = dict(os.environ)
env.update(BTS_ROOT=str(root.parent), BTS_PRECOMPOSE_WORKSPACE_DEMO='1',
           BTS_ENVIRONMENT=str(args.environment.resolve()),
           BTS_SESSION_LOG_DIR=str(out / 'logs'))
if args.verify_gpu_stack:
    env['BTS_VERIFY_GPU_STACK'] = '1'
with args.binary.open('rb') as source:
    binary_hash = hashlib.file_digest(source, 'sha256').hexdigest()
with private_gpu_gui(out, env, root) as (prefix, child):
    command = prefix + [str(args.binary.resolve()), '--workspace', '--no-splash',
                        '--gui-fix-demo', str(out), '--gui-fix-mesh', str(args.mesh.resolve()),
                        '--gui-fix-resolution', '128', '--environment', env['BTS_ENVIRONMENT'],
                        '--library-dir', str(out / 'library'), '--recovery-dir', str(out / 'recovery')]
    with (out / 'run.log').open('w') as log:
        result = subprocess.run(command, env=child, stdout=log, stderr=subprocess.STDOUT, timeout=600)
    log = (out / 'run.log').read_text()
    if result.returncode or 'BTS_PRECOMPOSE_WORKSPACE_PASS' not in log or 'BTS_SHUTDOWN clean' not in log:
        raise RuntimeError(f'Workspace proof failed ({result.returncode}): {log[-4000:]}')
    if args.verify_gpu_stack and 'BTS_GPU_STACK_SELF_TEST' not in log:
        raise RuntimeError('GPU stack oracle did not complete')
    receipt = json.loads((out / 'precompose-workspace.json').read_text())
    receipt.update(binary_sha256=binary_hash, clean_exit=True,
                   gpu_stack_oracle=args.verify_gpu_stack, eye_release=True)
    (out / 'precompose-workspace.json').write_text(json.dumps(receipt, indent=2)+'\n')
print('Pre-compose workspace: actual GPU capture, cached rows and eye release PASS')
