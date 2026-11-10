# SPDX-License-Identifier: GPL-3.0-or-later
"""Actual workspace retained-image Height controls. Native fresh cold is separate."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import signal
import subprocess
import time
from private_gpu_gui import private_gpu_gui

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--output', type=Path, required=True)
p.add_argument('--binary', type=Path, required=True)
p.add_argument('--expected-binary-sha256', required=True)
p.add_argument('--source-manifest', type=Path, required=True)
p.add_argument('--native-source-manifest', type=Path, help='Pin a previous native closure across only the four named reviewed UI test paths')
p.add_argument('--native-warm', type=Path, required=True)
p.add_argument('--environment', type=Path, required=True)
a = p.parse_args()
source = Path(__file__).resolve().parents[2]
bts = source.parent
out = a.output.resolve()
assert Path('/run/.containerenv').exists() and os.getpriority(os.PRIO_PROCESS, 0) == 19
assert out.is_relative_to((bts / 'build').resolve()) and not out.exists()

def sha(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()

def native_source_bridge(prior, current_sha, current_hashes, native_manifest=None):
    """Keep same-closure admission by default; allow only four named UI test paths."""
    if native_manifest is None:
        assert prior['source_manifest_sha256'] == current_sha, 'Native/UI must use the same frozen source closure'
        return {'mode': 'same-source-closure', 'native_source_manifest_sha256': current_sha,
                'ui_source_manifest_sha256': current_sha, 'actual_delta': []}
    native_manifest = Path(native_manifest).resolve()
    native_sha = sha(native_manifest)
    assert prior['source_manifest_sha256'] == native_sha, 'Native warm receipt does not match the supplied native source manifest'
    old = json.loads(native_manifest.read_text())['files']
    old_hashes = {name: value if isinstance(value, str) else value['sha256'] for name, value in old.items()}
    assert old_hashes.keys() == current_hashes.keys(), 'Native/UI source manifest keys differ'
    allowed = {'native/harness/NativeVectorMappingDemo.hh',
               'scripts/host/verify-native-vector-mapping-ui.py',
               'native/harness/NativeHeightImageUiDemo.hh',
               'scripts/host/verify-native-height-image-ui.py'}
    changed = sorted(name for name in old_hashes if old_hashes[name] != current_hashes[name])
    assert set(changed) <= allowed, 'Native/UI difference is outside the exact reviewed test paths: ' + str(changed)
    return {'mode': 'explicit-native-source-manifest', 'native_source_manifest': str(native_manifest),
            'native_source_manifest_sha256': native_sha, 'ui_source_manifest_sha256': current_sha,
            'actual_delta': [{'path': name, 'native_sha256': old_hashes[name],
                              'ui_sha256': current_hashes[name]} for name in changed]}

binary = a.binary.resolve()
assert sha(binary) == a.expected_binary_sha256
manifest_sha = sha(a.source_manifest)
frozen = json.loads(a.source_manifest.read_text())['files']
hashes = {name: value if isinstance(value, str) else value['sha256'] for name, value in frozen.items()}
required = {'native/harness/NativeHeightImageUiDemo.hh', 'native/host/materials/native/NativeHeightImageUiState.inc',
            'native/host/app/main.cc', 'native/harness/GuiFixDemo.cc',
            'native/tests/native_height_image_native_test.cc', 'native/tests/NativeHeightImageFixture.hh',
            'native/tests/NativeHeightImageReference.hh', 'scripts/host/verify-native-height-image-ui.py'}
assert required <= hashes.keys()
assert all(not Path(name).is_absolute() and '..' not in Path(name).parts and sha(source / name) == value
           for name, value in hashes.items())
warm = a.native_warm.resolve()
assert warm.is_relative_to((bts / 'build').resolve())
prior_path = warm / 'receipt.json'
prior_sha = sha(prior_path)
prior = json.loads(prior_path.read_text())
assert prior['phase'] == 'warm' and prior['passed'] and prior['actual_exit'] == 0 and prior['clean']
bridge = native_source_bridge(prior, manifest_sha, hashes, a.native_source_manifest)
project = warm / 'data/none-image-height-ui.blend'
fixture = warm / 'data/none-image-height-ui.json'
input_state = json.loads(fixture.read_text())
assert sha(project) == input_state['project_sha256']
assert input_state['tiles'] == [1001, 1002] and input_state['width'] == 128
assert input_state['layer_precision'] == 'L32F' and input_state['linear'] and input_state['repeat']
assert a.environment.is_file()
out.mkdir(parents=True)
(out / 'library').mkdir()
env = dict(os.environ, BTS_ROOT=str(bts), BTS_HOST_EXECUTABLE=str(binary),
           BTS_ENVIRONMENT=str(a.environment.resolve()), BTS_AW_HEIGHT_IMAGE_UI='1',
           BTS_AW_HEIGHT_IMAGE_UI_INPUT=str(fixture), BTS_SESSION_LOG_DIR=str(out / 'logs'),
           XDG_STATE_HOME=str(out / 'state'))
for key in ('DISPLAY', 'WAYLAND_DISPLAY', 'XAUTHORITY', 'LD_PRELOAD', 'BTS_VERIFY_GPU_STACK',
            'BTS_BM_ADJUSTMENT_DEMO', 'BTS_AW_VECTOR_UI', 'BTS_BJ_PROJECTION_UI', 'BTS_BN_SMART_MASK',
            'BTS_BN_SPMSK_UI', 'BTS_BK_FOLDER_MASK', 'BTS_STROKE_TARGET_PROFILE', 'BTS_BO_BRUSH_PRESETS'):
    env.pop(key, None)
for key, folder in {'XDG_STATE_HOME':'state', 'BLENDER_USER_CONFIG':'blender-config',
                    'CUDA_CACHE_PATH':'cuda-cache', '__GL_SHADER_DISK_CACHE_PATH':'nvidia-cache'}.items():
    (out / folder).mkdir()
    env[key] = str(out / folder)
command = ['bash', str(source / 'scripts/host/run-host.sh'), '--no-splash', '--workspace',
           '--gui-fix-demo', str(out), '--gui-fix-resolution', '128', '--open-document', str(project),
           '--autosave-seconds', '3600', '--library-dir', str(out / 'library')]
record = {'complete': False, 'actual_exit': None, 'binary_sha256': sha(binary),
          'source_manifest_sha256': manifest_sha, 'source_hashes': hashes, 'command': command,
          'native_source_bridge': bridge, 'native_warm_receipt_sha256': prior_sha,
          'input_project_sha256': sha(project), 'input_fixture_sha256': sha(fixture),
          'scope': 'Actual workspace Enable/Cancel/Undo/Redo and same-process Save/Open; native source-free fresh cold is separate'}
(out / 'receipt.json').write_text(json.dumps(record, indent=2) + '\n')
start = time.monotonic()
with private_gpu_gui(out, env, source) as (prefix, child), (out / 'run.log').open('w') as log:
    process = subprocess.Popen(prefix + command, cwd=source, env=child, stdout=log,
                               stderr=subprocess.STDOUT, start_new_session=True)
    record['pid'] = process.pid
    (out / 'receipt.json').write_text(json.dumps(record, indent=2) + '\n')
    try:
        code = process.wait(timeout=360)
    except subprocess.TimeoutExpired:
        record['timeout_seconds'] = 360
        os.killpg(process.pid, signal.SIGTERM)
        try:
            code = process.wait(timeout=15)
        except subprocess.TimeoutExpired:
            os.killpg(process.pid, signal.SIGKILL)
            code = process.wait()
record.update(actual_exit=code, seconds=time.monotonic() - start)
(out / 'receipt.json').write_text(json.dumps(record, indent=2) + '\n')
assert code == 0
log = (out / 'run.log').read_text()
assert 'BTS_SHUTDOWN clean' in log and 'AW_HEIGHT_IMAGE_UI_VERIFIED' in log
assert not any(marker in log for marker in ('BTS_HOST_FAILED', 'BTS_GL_ERROR', 'Not freed memory blocks'))
assert sha(binary) == a.expected_binary_sha256
assert sha(a.source_manifest) == manifest_sha and sha(prior_path) == prior_sha
if a.native_source_manifest is not None:
    assert sha(a.native_source_manifest) == bridge['native_source_manifest_sha256']
assert all(sha(source / name) == value for name, value in hashes.items())
proof = json.loads((out / 'aw-height-image-ui.json').read_text())
assert proof['passed'] and proof['cpu_composition_delta'] == proof['cpu_materialization_delta'] == 0
stages = {row['stage']: row for row in proof['checks']}
assert {'cancel-before-click', 'cancel-terminal-unchanged', 'independent-raw-H', 'undo', 'redo', 'saved'} <= stages.keys()
assert stages['cancel-before-click']['pending'] and stages['cancel-before-click']['button_visible']
assert stages['cancel-before-click']['button_enabled']
raw = [row for row in proof['checks'] if row['stage'] == 'independent-raw-H']
assert len(raw) == 2 and all(row['components'] == 32 and row['maximum_error'] < 3e-5 and row['source_min'] < 0 and row['source_max'] > 1 for row in raw)
assert len(proof['captures']) == 2
for name in ['height-enabled', 'height-reopened']:
    assert (out / (name + '.png')).is_file()
project_out = out / 'aw-height-image-ui.blend'
assert project_out.is_file() and project_out.stat().st_size > 0
pickers = proof['pickers']
assert [row['operation'] for row in pickers] == ['Save As', 'Open']
for row in pickers:
    assert row['filename'] == row['expected_filename'] == str(project_out)
    assert row['selected_files'] == [str(project_out)] and row['closed']
    assert row['activation']['owner_active'] and row['field_focus_before']
    assert row['button_focus'] and row['button_enabled'] and row['button_visible']
    assert all(row[name] == 1 for name in ['pressed', 'released', 'clicked', 'accepted'])
record.update(complete=True, clean_shutdown=True, proof_sha256=sha(out / 'aw-height-image-ui.json'),
              saved_project_sha256=sha(project_out), screenshots={name:sha(out / (name + '.png')) for name in ['height-enabled', 'height-reopened']})
(out / 'receipt.json').write_text(json.dumps(record, indent=2) + '\n')
print(json.dumps(record, indent=2))
