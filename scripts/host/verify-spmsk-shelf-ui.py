"""Explicit SPMSK Resource Import, pointer-null User Shelf, packed Open proof.

Run only in the coordinated native GPU lane. This script never clears or primes
source caches; each phase gets new private application/cache paths. A naturally
warming thumbnail job is observed separately from the catalog's preset pointer.
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

parser = argparse.ArgumentParser()
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--binary', type=Path, required=True)
parser.add_argument('--environment', type=Path, required=True)
parser.add_argument('--phase', choices=['import', 'shelf-cold', 'packed-cold'], required=True)
parser.add_argument('--prior', type=Path)
parser.add_argument('--source-dir', type=Path,
                    default=Path('/home/mlind/bts/comparison-1to1/smart-masks'))
args = parser.parse_args()
root = Path(__file__).resolve().parents[2]
out = args.output.resolve()
assert out.is_relative_to(root.parent / 'build'), 'Use a private build output'
assert Path('/run/.containerenv').exists(), 'Enter bts-build first'
assert os.getpriority(os.PRIO_PROCESS, 0) == 19, 'Run with nice 19'
assert not out.exists(), 'Retain every earlier run; choose a new output'
out.mkdir(parents=True)
binary = args.binary.resolve()

def sha(path):
    with path.open('rb') as source:
        return hashlib.file_digest(source, 'sha256').hexdigest()

def tree_manifest(directory):
    return {str(p.relative_to(directory)): {'sha256': sha(p), 'bytes': p.stat().st_size}
            for p in sorted(directory.rglob('*')) if p.is_file()}

binary_sha = sha(binary)
script_sha = sha(Path(__file__))
library = out / 'user-library'
project = out / 'imported-smart-masks.blend'
expected_hashes = {}
source_records = []
prior_receipt = None
if args.phase == 'import':
    fixtures = out / 'fixtures'
    fixtures.mkdir()
    for name in ['Rust.spmsk', 'Edge Rust.spmsk']:
        original = args.source_dir.resolve() / name
        copied = fixtures / name
        shutil.copy2(original, copied)
        original_sha = sha(original)
        assert sha(copied) == original_sha
        expected_hashes[name] = original_sha
        source_records.append({'original': str(original), 'copied': str(copied),
                               'sha256': original_sha, 'bytes': copied.stat().st_size})
else:
    assert args.prior, 'Fresh phases require a completed predecessor'
    prior = args.prior.resolve()
    assert prior.is_relative_to(root.parent / 'build'), 'Use a private predecessor'
    prior_receipt = json.loads((prior / 'receipt.json').read_text())
    assert prior_receipt['actual_exit'] == 0 and prior_receipt['clean_exit']
    assert prior_receipt['binary_sha256'] == binary_sha, 'Use one frozen coherent binary'
    expected_hashes = prior_receipt['expected_original_sha256']
    if args.phase == 'shelf-cold':
        assert prior_receipt['phase'] == 'import'
        old_library = Path(prior_receipt['user_library']).resolve()
        assert old_library.is_relative_to(prior)
        metadata = json.loads((old_library / 'library.json').read_text())
        assert len(metadata['resources']) == 2
        assert {row['filename']: row['sha256'] for row in metadata['resources']} == expected_hashes
        assert all(row['usages'] == ['smartmask'] for row in metadata['resources'])
        original_manifest = tree_manifest(old_library)
        assert 'library.json' in original_manifest
        shutil.copytree(old_library, library)
        assert tree_manifest(library) == original_manifest
        source_records.append({'copied_imported_user_library': str(old_library),
                               'manifest': original_manifest,
                               'warm_application_cache_copied': False})
    else:
        assert prior_receipt['phase'] in ('import', 'shelf-cold')
        project = Path(prior_receipt['project']).resolve()
        assert project.is_relative_to(prior), 'Use the predecessor packed project'
        assert project.is_file() and Path(str(project) + '.spmsk-witness.json').is_file()
        library.mkdir()
        assert not list(library.iterdir())

# No inherited test switches, user application paths, desktop display or source
# cache. The container client has already run before these APP-only paths exist.
env = {key: value for key, value in os.environ.items()
       if not key.startswith(('BTS_', 'XDG_', 'QT_', 'BLENDER_'))
       and key not in ('DISPLAY', 'WAYLAND_DISPLAY', 'XAUTHORITY', 'LD_PRELOAD')}
env.update(BTS_ROOT=str(root.parent), BTS_HOST_EXECUTABLE=str(binary),
           BTS_ENVIRONMENT=str(args.environment.resolve()),
           BTS_BN_SPMSK_UI='1', BTS_BN_SPMSK_PHASE=args.phase,
           BTS_BN_SPMSK_PROJECT=str(project), BTS_TEMPLATE_DIR=str(out / 'templates'),
           BTS_SESSION_LOG_DIR=str(out / 'logs'))
if args.phase == 'import':
    env['BTS_BN_SPMSK_RUST'] = str(out / 'fixtures/Rust.spmsk')
    env['BTS_BN_SPMSK_EDGE_RUST'] = str(out / 'fixtures/Edge Rust.spmsk')
for key, folder in [('BLENDER_USER_CONFIG', 'blender-config'),
                    ('CUDA_CACHE_PATH', 'cuda-cache'),
                    ('__GL_SHADER_DISK_CACHE_PATH', 'nvidia-cache')]:
    (out / folder).mkdir()
    env[key] = str(out / folder)
options = ['--workspace', '--gui-fix-demo', str(out), '--gui-fix-mesh',
           str(root / 'fixtures/single_tile.obj'), '--gui-fix-resolution', '128']
if args.phase == 'packed-cold':
    options += ['--open-document', str(project)]
start = time.monotonic()
with private_gpu_gui(out / 'private', env, root) as (prefix, child):
    assert not list(Path(child['BTS_RESOURCE_CACHE']).iterdir())
    assert 'BTS_VERIFY_GPU_STACK' not in child
    command = prefix + ['nice', '-n', '19', 'bash', str(root / 'scripts/host/run-host.sh'),
                        '--no-splash', *options, '--autosave-seconds', '3600',
                        '--library-dir', str(library)]
    (out / 'command.json').write_text(json.dumps(command, indent=2) + '\n')
    with (out / 'run.log').open('w') as log:
        process = subprocess.Popen(command, env=child, stdout=log, stderr=subprocess.STDOUT)
        (out / 'pid.json').write_text(json.dumps({'pid': process.pid}) + '\n')
        timed_out = False
        try:
            code = process.wait(timeout=900)
        except subprocess.TimeoutExpired:
            timed_out = True
            process.terminate()
            try:
                code = process.wait(timeout=15)
            except subprocess.TimeoutExpired:
                process.kill()
                code = process.wait()
    log = (out / 'run.log').read_text(errors='replace')
    receipt = {'phase': args.phase, 'pid': process.pid, 'actual_exit': code,
               'timed_out': timed_out, 'seconds': time.monotonic() - start,
               'binary_sha256': binary_sha, 'binary_unchanged': sha(binary) == binary_sha,
               'verifier_sha256': script_sha, 'verifier_unchanged': sha(Path(__file__)) == script_sha,
               'clean_exit': 'BTS_SHUTDOWN clean' in log,
               'user_library': str(library), 'project': str(project),
               'expected_original_sha256': expected_hashes, 'source_records': source_records,
               'private_app_and_source_cache_fresh': True,
               'warm_resource_cache_preserved_and_unused': True,
               'source_cache_claim': 'No postactivation fresh-HDF-read claim. Record actual pointer/cache observations; natural thumbnail preparation is allowed.',
               'display': json.loads((out / 'private/private-display.json').read_text()),
               'command': command}
    (out / 'receipt.json').write_text(json.dumps(receipt, indent=2) + '\n')
assert code == 0 and not timed_out, receipt
assert receipt['clean_exit'] and receipt['binary_unchanged'] and receipt['verifier_unchanged'], receipt
assert not re.search(r'BTS_HOST_FAILED|BTS_DRAW_FAILED|BTS_GPU_AUTHORING_CPU_READ|'
                     r'BTS_GPU_CPU_FALLBACK|BTS_GPU_STACK_FALLBACK|BTS_THUMBNAIL_ERROR|'
                     r'Shader compil(?:ation|e) failed', log, re.I), 'Retained native failure'
proof = json.loads((out / 'spmsk-shelf.json').read_text())
assert not proof['failure'] and proof['phase'] == args.phase
assert 'BTS_SPMSK_SHELF_PASS' in log
assert proof['persistence']['targets'] == 4 and proof['persistence']['sources'] == 2
assert proof['persistence']['parameters_original_bytes_and_resolved_pixels_equal']
observations = proof['source_observations']
initial = next(row for row in observations if row.get('stage') == 'initial-process-catalog')
if args.phase == 'import':
    assert any(row.get('stage') == 'new-ui-generator-defaults-mask-and-content' for row in observations), 'Fresh UI Generator defaults were not exercised'
if args.phase != 'packed-cold':
    assert len(proof['checks']) == 4
    assert {(row['preset'], row['folder']) for row in proof['checks']} == {
        (name, folder) for name in ['Rust', 'Edge Rust'] for folder in (False, True)}
    activations = [row for row in observations if 'before_activation' in row]
    assert len(activations) == 4
    assert all(row['state_after_activation']['spsm_application_polls'] > 0 for row in activations)
    for row in activations:
        source = row['before_activation']
        assert source['source_sha256'] == expected_hashes[row['preset'] + '.spmsk']
        assert isinstance(source['prepared_source_cached'], bool)
    if args.phase == 'shelf-cold':
        assert prior_receipt['pid'] != receipt['pid'], 'Pointer-null witness needs a real new process'
        cold = [row for row in activations if row['first_source_application']]
        assert len(cold) == 2 and {row['folder'] for row in cold} == {False, True}
        assert all(not row['before_activation']['smart_mask_present'] and
                   row['before_activation']['smart_mask_pointer'] == '0' for row in cold)
    witness = json.loads(Path(str(project) + '.spmsk-witness.json').read_text())
    assert {row['filename']: row['sha256'] for row in witness['sources']} == expected_hashes
else:
    assert initial['catalog']['user_sources'] == []
# CPU composition/materialization counters are actual production counters.
# Observe interactive checkpoints; source parsing remains a worker operation.
keys = ['cpu_materializations', 'cpu_layer_compositions']
baseline = {key: initial['state'][key] for key in keys}
for row in proof['captures']:
    assert all(row[key] == baseline[key] for key in keys), (row['stage'], baseline, row)
print(json.dumps(receipt, indent=2))
