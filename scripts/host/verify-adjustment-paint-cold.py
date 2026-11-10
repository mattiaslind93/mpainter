# SPDX-License-Identifier: GPL-3.0-or-later
"""Fresh-process canonical BM archive and GPU publication proof; private display only."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import signal
import subprocess
import time
from private_gpu_gui import private_gpu_gui

p = argparse.ArgumentParser()
p.add_argument('--warm', type=Path, required=True)
p.add_argument('--output', type=Path, required=True)
p.add_argument('--expected-binary-sha256', required=True)
a = p.parse_args()
root = Path(__file__).resolve().parents[2]
bts = Path(os.environ.get('BTS_ROOT', root.parent)).resolve()
assert Path('/run/.containerenv').exists() and os.getpriority(os.PRIO_PROCESS, 0) == 19
warm, output = a.warm.resolve(), a.output.resolve()
assert output.is_relative_to((bts / 'build').resolve()) and not output.exists()
binary = Path(os.environ['BTS_HOST_EXECUTABLE']).resolve()
def sha(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()
witness = warm / 'bm-adjustment-witness.json'
prior = json.loads((warm / 'receipt.json').read_text())
assert prior['complete'] and prior['actual_exit'] == 0 and prior['clean_shutdown']
assert prior['binary_sha256'] == a.expected_binary_sha256 == sha(binary)
expected = json.loads(witness.read_text())
project = Path(expected['document_path']).resolve()
assert project.is_relative_to(warm) and sha(project) == expected['packed_file_sha256']
assert expected['tiles'] == [1001, 1002] and len(expected['gpu']) == 4
owned = ['native/harness/AdjustmentPaintColdProof.hh', 'native/harness/ViewportHeadlessDemo.cc',
         'native/harness/GpuAdjustmentPaintTest.hh', 'native/host/GpuLayerPreview.cc',
         'native/host/gpu/GpuAdjustmentPages.hh', 'scripts/host/verify-adjustment-paint-cold.py']
source_hashes = {name: sha(root / name) for name in owned}
output.mkdir(parents=True)
library = output / 'library'
library.mkdir()
environment = Path(os.environ.get('BTS_ENVIRONMENT', bts / 'build/shared-fixtures/TT_environmentLights_v001.hdr')).resolve()
assert environment.is_file()
for folder in ("blender-config", "cuda", "nvidia"):
    (output / folder).mkdir()
env = {k: v for k, v in os.environ.items() if not k.startswith(('BTS_', 'XDG_', 'BLENDER_', 'QT_')) and k not in ('DISPLAY', 'WAYLAND_DISPLAY', 'XAUTHORITY', 'LD_PRELOAD')}
env.update(BTS_ROOT=str(bts), BTS_HOST_EXECUTABLE=str(binary), BTS_ENVIRONMENT=str(environment),
           BTS_BM_ADJUSTMENT_COLD='1', BTS_BM_ADJUSTMENT_EXPECTED=str(witness),
           BTS_SESSION_LOG_DIR=str(output / 'logs'), BLENDER_USER_CONFIG=str(output / 'blender-config'),
           CUDA_CACHE_PATH=str(output / 'cuda'), __GL_SHADER_DISK_CACHE_PATH=str(output / 'nvidia'))
command = ['bash', str(root / 'scripts/host/run-host.sh'), '--no-splash', '--headless-viewport-test', str(output),
           '--open-document', str(project), '--autosave-seconds', '3600', '--library-dir', str(library)]
receipt = {'complete': False, 'actual_exit': None, 'binary_sha256': sha(binary), 'warm_pid': prior['pid'],
           'source_hashes': source_hashes, 'witness_sha256': sha(witness), 'project_sha256': sha(project),
           'command': command, 'environment_sha256': sha(environment)}
def save():
    (output / 'receipt.json').write_text(json.dumps(receipt, indent=2) + '\n')
save()
start = time.monotonic()
with private_gpu_gui(output / 'private', env, root) as (prefix, child), (output / 'run.log').open('w') as log:
    assert not list(Path(child['BTS_RESOURCE_CACHE']).iterdir()) and not list(library.iterdir())
    process = subprocess.Popen(prefix + command, cwd=root, env=child, stdout=log, stderr=subprocess.STDOUT, start_new_session=True)
    receipt['pid'] = process.pid
    assert process.pid != prior['pid']
    save()
    try:
        code = process.wait(timeout=240)
    except subprocess.TimeoutExpired:
        os.killpg(process.pid, signal.SIGTERM)
        try:
            code = process.wait(timeout=15)
        except subprocess.TimeoutExpired:
            os.killpg(process.pid, signal.SIGKILL)
            code = process.wait()
        receipt['timeout_seconds'] = 240
receipt.update(actual_exit=code, seconds=time.monotonic() - start)
save()
assert code == 0, receipt
log = (output / 'run.log').read_text()
assert 'BTS_SHUTDOWN clean' in log and 'BTS_BM_ADJUSTMENT_COLD_VERIFIED' in log
assert not any(marker in log for marker in ('BTS_HOST_FAILED', 'BTS_GL_ERROR', 'BTS_BRUSH_CPU_FALLBACK', 'Not freed memory blocks'))
proof = json.loads((output / 'bm-adjustment-cold.json').read_text())
assert proof['data']['data_first'] and proof['data']['explicit_draws_before_validation'] == 0
assert proof['data']['transient_gpu_recipes'] == 0 and proof['data']['canonical_stored_pages'] > 0
assert proof['normal_frames'] >= 8 and proof['components'] == 64 and proof['maximum_error'] < 3e-6
assert proof['cpu_materializations'] == proof['cpu_layer_compositions'] == 0
assert sha(binary) == receipt['binary_sha256'] and sha(witness) == receipt['witness_sha256'] and sha(project) == receipt['project_sha256']
assert source_hashes == {name: sha(root / name) for name in owned}
receipt.update(complete=True, clean_shutdown=True, proof_sha256=sha(output / 'bm-adjustment-cold.json'),
               scope='Fresh process, empty resource cache/library; canonical data first, then ordinary EEVEE publication. GPU numerical/export/UI acceptance recorded separately.')
save()
print(json.dumps(receipt, indent=2))
