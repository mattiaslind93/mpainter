# SPDX-License-Identifier: GPL-3.0-or-later
"""Runtime-only BJ real Fill-controls / packed mapping UI proof.

Use the owner's frozen coherent host after explicit GPU-lane release. This script
never builds or installs anything. Generate the owned fixture separately with
native/tests/native_material_projection_fixture.py in approved background Blender.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import time
from private_gpu_gui import private_gpu_gui
from native_material_projection_admission import validate_fixture, validate_predecessor, validate_cold_frame_progression

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--binary', type=Path, required=True)
p.add_argument('--expect-binary', required=True)
p.add_argument('--source-manifest', type=Path, required=True,
               help='Frozen owner source manifest: files={relativepath: sha256 or {sha256}}')
p.add_argument('--environment', type=Path, required=True)
p.add_argument('--output', type=Path, required=True)
p.add_argument('--kind', choices=['UV', 'Object', 'Generated', 'uniform'], required=True)
p.add_argument('--phase', choices=['warm', 'cold', 'data-cold'], required=True)
p.add_argument('--fixtures', type=Path)
p.add_argument('--prior', type=Path)
p.add_argument('--timeout', type=int, default=1800)
a = p.parse_args()
root = Path(__file__).resolve().parents[2]
out = a.output.resolve()
assert out.is_relative_to(root.parent / 'build'), 'Private build output required'
assert not out.exists(), 'Preserve prior receipts; choose a new output'
assert Path('/run/.containerenv').exists(), 'Enter bts-build before APP environment changes'
assert os.getpriority(os.PRIO_PROCESS, 0) == 19, 'Run nice 19'
assert a.environment.resolve().is_file(), 'Approved explicit HDRI required'

def sha(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()

def require_float32_exr(path):
    # Header-only check, mirroring the EXR chlist contract in FlattenExport's
    # independent codec test. A .exr filename does not establish Float32 data.
    import struct
    data = path.read_bytes()
    assert data[:4] == bytes.fromhex('762f3101')
    cursor = 8
    def cstring(at):
        end = data.index(0, at)
        return data[at:end], end + 1
    while data[cursor]:
        name, cursor = cstring(cursor)
        kind, cursor = cstring(cursor)
        size = struct.unpack_from('<I', data, cursor)[0]
        cursor += 4
        end = cursor + size
        assert end <= len(data)
        if name == b'channels':
            assert kind == b'chlist'
            types = []
            while cursor < end and data[cursor]:
                _, cursor = cstring(cursor)
                types.append(struct.unpack_from('<I', data, cursor)[0])
                cursor += 16
            assert types and all(value == 2 for value in types), types
            return types
        cursor = end
    raise AssertionError('EXR has no channel type list')

binary = a.binary.resolve()
assert sha(binary) == a.expect_binary, 'Frozen coherent binary changed'
manifest_path = a.source_manifest.resolve()
manifest = json.loads(manifest_path.read_text())['files']
required = {'native/host/main.cc', 'native/harness/GuiFixDemo.cc',
            'native/harness/NativeMaterialProjectionDemo.hh',
            'native/harness/NativeMaterialProjectionProofActions.inc',
            'native/harness/NativeMaterialProjectionWitness.hh',
            'native/host/NativeMaterialProjectionState.inc',
            'native/harness/NativeMaterialProjectionColdProof.hh',
            'native/tests/native_material_projection_fixture.py',
            'scripts/host/verify-native-material-projection.py',
            'scripts/host/private_gpu_gui.py',
            'scripts/host/native_material_projection_admission.py',
            'native/tests/native_material_projection_admission_test.py'}
assert required <= manifest.keys(), 'Freeze actual actor/integration/wrapper bytes'
source_hashes = {name: value if isinstance(value, str) else value['sha256']
                 for name, value in manifest.items()}
assert all(not Path(name).is_absolute() and '..' not in Path(name).parts
           for name in source_hashes), 'Source paths must be repository-relative'
assert all(sha(root / name) == value for name, value in source_hashes.items())
out.mkdir(parents=True)
(out / 'source-guard.json').write_text(json.dumps(source_hashes, indent=2) + '\n')
library = out / 'user-library'
library.mkdir()
hidden = None
prior_receipt = None
source_record = {}

if a.phase == 'warm':
    assert a.fixtures and not a.prior
    original = a.fixtures.resolve()
    assert original.is_relative_to(root.parent / 'build'), 'Use only owned generated build fixtures'
    fixture, original_hashes = validate_fixture(
        original, a.kind, source_hashes['native/tests/native_material_projection_fixture.py'])
    fixtures = out / 'fixtures'
    shutil.copytree(original, fixtures)
    source_record = {name: sha(fixtures / name) for name in
                     ['owned-projection.blend', 'owned.obj', 'owned.mtl',
                      'owned-uniform.png', 'owned-height.exr', 'fixture.json']}
    assert source_record == original_hashes
    validate_fixture(fixtures, a.kind, source_hashes['native/tests/native_material_projection_fixture.py'])
    height_source_types = require_float32_exr(fixtures / 'owned-height.exr')
    mesh = fixtures / 'owned.obj'
    source = fixtures / 'owned-projection.blend'
    project = None
else:
    assert a.prior and not a.fixtures
    prior = a.prior.resolve()
    assert prior.is_relative_to(root.parent / 'build')
    prior_receipt = json.loads((prior / 'receipt.json').read_text())
    assert prior_receipt['actual_exit'] == 0 and prior_receipt['clean_exit']
    assert prior_receipt['phase'] == 'warm' and prior_receipt['kind'] == a.kind
    assert prior_receipt['binary_sha256'] == a.expect_binary
    assert prior_receipt['source_hashes'] == source_hashes, 'Warm/cold actual source differs'
    expected = json.loads((prior / 'native-projection-ui.json').read_text())
    validate_predecessor(prior_receipt, expected, a.kind, a.expect_binary, source_hashes)
    fixture, original_hashes = validate_fixture(
        prior / 'fixtures', a.kind, source_hashes['native/tests/native_material_projection_fixture.py'])
    assert original_hashes == prior_receipt['fixture_sha256']
    copied = out / 'packed'
    copied.mkdir()
    for checkpoint in expected['checkpoints']:
        original = Path(checkpoint['project']).resolve()
        assert original.is_relative_to(prior) and original.is_file()
        destination = copied / original.name
        shutil.copy2(original, destination)
        shutil.copy2(Path(str(original) + '.bj-witness.json'),
                     Path(str(destination) + '.bj-witness.json'))
        assert sha(destination) == sha(original)
        checkpoint['project'] = str(destination)
    expected_file = out / 'warm-checkpoints.json'
    expected_file.write_text(json.dumps(expected, indent=2) + '\n')
    project = Path(expected['checkpoints'][0]['project'])
    source_record = prior_receipt['fixture_sha256']
    height_source_types = prior_receipt['ordinary_height_exr_channel_types']
    # Hide only this test's own copied fixture directory. Preserve the first
    # negative and restore originals after the process, even on timeout/error.
    source = prior / 'fixtures' / 'owned-projection.blend'
    hidden = (prior / 'fixtures', prior / 'fixtures.hidden-for-bj-cold')
    assert hidden[0].is_dir() and not hidden[1].exists()
    mesh = root / 'native/tests/fixtures/single_tile.obj'  # Unused: cold opens packed geometry.

env = {key: value for key, value in os.environ.items()
       if not key.startswith(('BTS_', 'XDG_', 'QT_', 'BLENDER_'))
       and key not in ('DISPLAY', 'WAYLAND_DISPLAY', 'XAUTHORITY', 'LD_PRELOAD')}
env.update(BTS_ROOT=str(root.parent), BTS_HOST_EXECUTABLE=str(binary),
           BTS_ENVIRONMENT=str(a.environment.resolve()), BTS_BJ_PROJECTION_UI='1',
           BTS_BJ_PROJECTION_KIND=a.kind, BTS_BJ_PROJECTION_PHASE=a.phase,
           BTS_TEMPLATE_DIR=str(out / 'templates'), BTS_SESSION_LOG_DIR=str(out / 'logs'),
           BTS_FLATTEN_VERIFY_OUTPUT='1', BTS_PERFORMANCE_PROFILE='1')
if a.phase == 'warm':
    env['BTS_BJ_PROJECTION_SOURCE'] = str(source)
    env['BTS_BJ_PROJECTION_HEIGHT'] = str(fixtures / 'owned-height.exr')
else:
    env['BTS_BJ_PROJECTION_EXPECTED'] = str(expected_file)
for key, folder in [('BLENDER_USER_CONFIG', 'blender-config'),
                    ('CUDA_CACHE_PATH', 'cuda-cache'),
                    ('__GL_SHADER_DISK_CACHE_PATH', 'nvidia-cache')]:
    (out / folder).mkdir()
    env[key] = str(out / folder)
options = ['--workspace', '--gui-fix-demo', str(out), '--gui-fix-mesh', str(mesh),
           '--gui-fix-resolution', '128', '--autosave-seconds', '3600',
           '--library-dir', str(library)]
if project:
    options += ['--open-document', str(project)]
if a.phase == 'data-cold':
    env['BTS_BJ_PROJECTION_DATA_COLD'] = '1'
    options = ['--headless-viewport-test', str(out), '--open-document', str(project),
               '--autosave-seconds', '3600', '--library-dir', str(library)]
code = None
private_ready = False
timed_out = False
start = time.monotonic()
try:
    if hidden:
        hidden[0].rename(hidden[1])
        assert not source.exists() and not hidden[0].exists()
    with private_gpu_gui(out / 'private', env, root) as (prefix, child):
        assert not list(Path(child['BTS_RESOURCE_CACHE']).iterdir())
        assert not list(library.iterdir())
        assert 'BTS_VERIFY_GPU_STACK' not in child
        private_ready = True
        # Record actual post-profile/VirtualGL child ownership, then exec the
        # unchanged guarded launcher in the SAME PID. No display/guard override.
        ownership = out / 'launch-ownership-probe.py'
        ownership.write_text("""import json, os, sys
from pathlib import Path
sys.path.insert(0, sys.argv[1])
from session_guard import owned_private_xvfb
caller = os.getppid()
server = int(os.environ.get('BTS_PRIVATE_XVFB_PID', '0'))
def proc_parent(pid):
    return next(int(line.split(':', 1)[1]) for line in Path(f'/proc/{pid}/status').read_text().splitlines() if line.startswith('PPid:'))
config = Path(os.environ.get('XDG_CONFIG_HOME', ''))
display = os.environ.get('DISPLAY', '')
record = {'caller_pid': caller, 'caller_ppid': proc_parent(caller),
          'server_pid': server, 'server_ppid': proc_parent(server),
          'caller_uid': Path(f'/proc/{caller}').stat().st_uid,
          'server_uid': Path(f'/proc/{server}').stat().st_uid,
          'uid': os.getuid(), 'display': display,
          'qt_platform': os.environ.get('QT_QPA_PLATFORM'),
          'wayland_present': bool(os.environ.get('WAYLAND_DISPLAY')),
          'config_canonical': str(config.resolve()),
          'build_canonical': str((Path(os.environ['BTS_ROOT']) / 'build').resolve()),
          'x_lock': Path('/tmp/.X' + display.removeprefix(':').removesuffix('.0') + '-lock').read_text().strip(),
          'owned_private_xvfb': owned_private_xvfb(caller_pid=caller)}
Path(sys.argv[2]).write_text(json.dumps(record, indent=2) + '\\n')
if not record['owned_private_xvfb']:
    raise SystemExit('Private launch ownership preflight rejected; guarded host was not started')
""")
        command = prefix + ['nice', '-n', '19', 'bash', '-c',
                            'python3 "$1" "$2" "$3" || exit $?; shift 3; exec bash "$@"',
                            'bts-projection-owned', str(ownership), str(root / 'scripts/host'),
                            str(out / 'launch-ownership.json'), str(root / 'scripts/host/run-host.sh'),
                            '--no-splash', *options]
        (out / 'command.json').write_text(json.dumps(command, indent=2) + '\n')
        with (out / 'run.log').open('w') as log:
            process = subprocess.Popen(command, env=child, stdout=log, stderr=subprocess.STDOUT)
            (out / 'pid.json').write_text(json.dumps({'launcher_pid': process.pid}) + '\n')
            try:
                code = process.wait(timeout=a.timeout)
            except subprocess.TimeoutExpired:
                timed_out = True
                process.terminate()
                try:
                    code = process.wait(timeout=15)
                except subprocess.TimeoutExpired:
                    process.kill()
                    code = process.wait()
finally:
    if hidden and hidden[1].exists():
        assert not hidden[0].exists()
        hidden[1].rename(hidden[0])
    log = (out / 'run.log').read_text(errors='replace') if (out / 'run.log').exists() else ''
    proof_path = out / ('native-projection-cold-data.json' if a.phase == 'data-cold' else 'native-projection-ui.json')
    proof = json.loads(proof_path.read_text()) if proof_path.exists() else {}
    receipt = {'phase': a.phase, 'kind': a.kind, 'actual_exit': code, 'timed_out': timed_out,
               'native_pid': proof.get('pid'), 'seconds': time.monotonic() - start,
               'binary_sha256': a.expect_binary, 'binary_unchanged': sha(binary) == a.expect_binary,
               'source_hashes': source_hashes,
               'source_unchanged': all(sha(root / name) == value for name, value in source_hashes.items()),
               'clean_exit': 'BTS_SHUTDOWN clean' in log, 'fixture_sha256': source_record,
               'fixture_unchanged': all(sha((hidden[0] if hidden else fixtures) / name) == digest
                                        for name, digest in source_record.items()),
               'ordinary_height_exr_channel_types': height_source_types,
               'uniform_packed_image_reference': fixture['uniform_packed_image'],
               'fixture_original_blend_sha256': fixture['source_blend_sha256'],
               'prior': str(a.prior.resolve()) if a.prior else None,
               'fresh_application_source_cache_and_user_library': private_ready,
               'cold_source_directory_hidden_during_process': bool(hidden),
               'cold_warm_cache_copied': False,
               'cold_scope': 'Real fresh process; original copied source path absent, empty private source cache/library. Packed data validated before explicit diagnostic reads. Host Open may prepare GPU resources before actor validation.'}
    (out / 'receipt.json').write_text(json.dumps(receipt, indent=2) + '\n')
assert code == 0 and not timed_out, receipt
assert receipt['clean_exit'] and receipt['binary_unchanged'] and receipt['source_unchanged'] and receipt['fixture_unchanged'], receipt
if a.phase == 'data-cold':
    assert 'BTS_BJ_PROJECTION_COLD_DATA_PASS' in log
    count = 14
    validate_cold_frame_progression(proof)
    assert proof['maximum_error'] < 3e-6
    assert all(row['data']['source_bytes_parameters_and_resolved_pixels_equal'] and
               row['data']['explicit_eevee_frames_before_validation'] == 0 and
               row['gpu_components'] >= 120 and row['maximum_error'] < 3e-6
               for row in proof['checkpoints'])
else:
    assert proof['failure'] == '' and proof['kind'] == a.kind and proof['phase'] == a.phase
    assert 'BTS_BJ_PROJECTION_PASS' in log
assert not re.search(r'BTS_HOST_FAILED|BTS_DRAW_FAILED|BTS_GPU_AUTHORING_CPU_READ|'
                     r'BTS_GPU_CPU_FALLBACK|BTS_GPU_STACK_FALLBACK|BTS_THUMBNAIL_ERROR|'
                     r'Shader compil(?:ation|e) failed', log, re.I)
count = 14
if a.phase == 'data-cold':
    assert prior_receipt['native_pid'] != receipt['native_pid']
    print(json.dumps(receipt, indent=2))
    raise SystemExit(0)
assert len(proof['checkpoints']) == count
assert {(row['policy'], row['mode']) for row in proof['checkpoints']} == {
    (policy, mode) for policy in range(2) for mode in range(7)}
assert all(row['controls']['fillProjectionMode']['index'] == row['mode'] for row in proof['checkpoints'])
if a.phase == 'warm':
    checks = proof['checks']
    if a.kind == 'uniform':
        assert any(row['stage'] == 'ordinary-bitmap-height-oracle' and row['minimum'] < 0 and row['maximum'] > 1 for row in checks)
    assert all(any(row['stage'] == name for row in checks) for name in
               ['uv-gizmo-drag', 'spatial-gizmo-drag', 'warp-control-drag',
                'triplanar-hardness', 'mode6-ignores-fill-2d', 'gpu-flatten-export',
                'cycles-edited', 'cycles-undo', 'cycles-redo', 'gpu-flatten'])
    for row in checks:
        if 'changed_required' in row:
            assert row['zero_shader_recompiles'] and row['zero_cpu_composition']
            if row['changed_required']:
                assert row['gpu_max_delta'] > 2e-6
    interactive = [row for row in proof['captures']
                   if row['stage'] == 'initial-imported' or row['stage'].startswith('policy')]
    assert len(interactive) == count + 1
    for counter in ['cpu_materializations', 'cpu_layer_compositions']:
        assert len({row['diagnostic'][counter] for row in interactive}) == 1
    assert len([row for row in checks if row['stage'].endswith('physical-size')]) == 12
else:
    assert prior_receipt['native_pid'] != receipt['native_pid'], 'Cold must be another native process'
    checks = [row for row in proof['checks'] if row['stage'] == 'cold-gpu-checkpoint']
    assert len(checks) == count and all(row['max_error'] < 3e-6 for row in checks)
    data = [row for row in proof['checks'] if row['stage'] == 'packed-data-before-explicit-gpu-read']
    assert len(data) == count and all(row['proof']['source_bytes_parameters_and_resolved_pixels_equal'] for row in data)
print(json.dumps(receipt, indent=2))
