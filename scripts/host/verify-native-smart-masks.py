"""BN native mask UI and separate packed cold Open on an owned GPU display."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import time
from private_gpu_gui import private_gpu_gui

parser = argparse.ArgumentParser()
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--binary', type=Path, required=True)
parser.add_argument('--environment', type=Path, required=True)
parser.add_argument('--phase', choices=['warm', 'cold'], required=True)
parser.add_argument('--warm', type=Path)
args = parser.parse_args()
root = Path(__file__).resolve().parents[2]
out = args.output.resolve()
assert out.is_relative_to(root.parent / 'build'), 'Use a private build output'
assert Path('/run/.containerenv').exists(), 'Enter bts-build first'
assert os.getpriority(os.PRIO_PROCESS, 0) == 19, 'Run with nice 19'
assert not out.exists(), 'Preserve earlier proof; use a new output directory'
out.mkdir(parents=True)
binary = args.binary.resolve()

def sha(path):
    with path.open('rb') as source:
        return hashlib.file_digest(source, 'sha256').hexdigest()

before_sha = sha(binary)
env = {key: value for key, value in os.environ.items()
       if not key.startswith(('BTS_', 'XDG_', 'QT_', 'BLENDER_'))
       and key not in ('DISPLAY', 'WAYLAND_DISPLAY', 'XAUTHORITY', 'LD_PRELOAD')}
env.update(BTS_ROOT=str(root.parent), BTS_HOST_EXECUTABLE=str(binary),
           BTS_ENVIRONMENT=str(args.environment.resolve()),
           BTS_SESSION_LOG_DIR=str(out / 'logs'), BTS_TEMPLATE_DIR=str(out / 'templates'))
for key, name in [('BLENDER_USER_CONFIG', 'blender-config'),
                  ('CUDA_CACHE_PATH', 'cuda-cache'),
                  ('__GL_SHADER_DISK_CACHE_PATH', 'nvidia-cache')]:
    (out / name).mkdir()
    env[key] = str(out / name)
if args.phase == 'warm':
    env.update(BTS_BN_SMART_MASK='1', BTS_PERFORMANCE_PROFILE='1', BTS_SESSION_PROFILE='1')
    options = ['--workspace', '--gui-fix-demo', str(out), '--gui-fix-mesh',
               str(root / 'native/tests/fixtures/single_tile.obj'), '--gui-fix-resolution', '128']
else:
    assert args.warm, 'Cold phase needs the completed warm project'
    warm = args.warm.resolve()
    prior = json.loads((warm / 'receipt.json').read_text())
    assert prior['actual_exit'] == 0 and prior['clean_exit'] and prior['phase'] == 'warm'
    assert prior['binary_sha256'] == before_sha, 'Cold must use the same frozen native binary'
    project = warm / 'smart-masks.blend'
    assert project.is_file() and Path(str(project) + '.smart-mask-witness.json').is_file()
    # Keep the warm cache as retained evidence, outside its original location.
    # The cold process receives its own entirely new application/cache paths.
    cache = warm / 'private/resources'
    assert cache.is_dir()
    held = cache.with_name('resources.held')
    assert not held.exists()
    cache.rename(held)
    env['BTS_SMART_MASK_COLD_OPEN'] = '1'
    options = ['--headless-viewport-test', str(out), '--open-document', str(project)]
private_dir = out / 'private'
start = time.monotonic()
with private_gpu_gui(private_dir, env, root) as (prefix, child):
    assert not list(Path(child['BTS_RESOURCE_CACHE']).iterdir())
    assert 'BTS_VERIFY_GPU_STACK' not in child, 'Do not warm the catalog/self-test in cold proof'
    command = prefix + ['nice', '-n', '19', 'bash', str(root / 'scripts/host/run-host.sh'),
                        '--no-splash', *options, '--autosave-seconds', '3600',
                        '--library-dir', str(out / 'library')]
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
               'binary_sha256': before_sha, 'binary_unchanged': sha(binary) == before_sha,
               'clean_exit': 'BTS_SHUTDOWN clean' in log,
               'private_cache_empty_before_launch': True,
               'display': json.loads((private_dir / 'private-display.json').read_text()),
               'command': command}
    (out / 'receipt.json').write_text(json.dumps(receipt, indent=2) + '\n')
assert code == 0 and not timed_out, receipt
assert receipt['clean_exit'] and receipt['binary_unchanged'], receipt
assert not re.search(r'BTS_HOST_FAILED|BTS_DRAW_FAILED|BTS_GPU_AUTHORING_CPU_READ|'
                     r'BTS_GPU_CPU_FALLBACK|BTS_GPU_STACK_FALLBACK|BTS_THUMBNAIL_ERROR|'
                     r'Shader compil(?:ation|e) failed', log, re.I), 'Retained native failure in run.log'
if args.phase == 'warm':
    proof = json.loads((out / 'smart-masks.json').read_text())
    assert not proof['failure'] and len(proof['checks']) == 16
    assert 'BTS_BN_SMART_MASK_UI_PASS' in log
    assert len({(row['preset'], row['folder']) for row in proof['checks']}) == 16
    assert proof['captures'], 'Missing actual metadata captures'
    assert all(row['cpu_materializations'] == 0 and row['cpu_layer_compositions'] == 0
               for row in proof['captures']), 'Interactive mask CPU evaluation detected'
    assert all(row['handler_return_ms'] >= 0 and row['drop_to_settled_observation_ms'] >= row['handler_return_ms']
               for row in proof['checks'])
    assert len(proof['property_latency']) == 48
    assert len({(row['preset'], row['folder'], row['field']) for row in proof['property_latency']}) == 48
    assert all(row['handler_ms'] >= 0 for row in proof['property_latency'])
    names = {'Rust', 'Edge Wear', 'Crease Dirt', 'Moss', 'Ground Dirt',
             'Scratches', 'Leaks & Stains', 'Surface Dust'}
    values = {}
    for match in re.finditer(r'BTS_SMART_MASK_THUMBNAIL_VALUES asset=(.*?) samples=(\d+) mask_min=(\S+) mask_max=(\S+) mask_mean=(\S+) display_min=(\S+) display_max=(\S+)', log):
        name = match[1]
        if name in names:
            values[name] = dict(zip(('samples', 'mask_min', 'mask_max', 'mask_mean',
                                      'display_min', 'display_max'), map(float, match.groups()[1:])))
    assert set(values) == names, 'Missing evaluated native preview coverage metadata'
    assert all(v['samples'] > 0 and v['mask_max'] - v['mask_min'] > .01
               and v['display_max'] - v['display_min'] > .05 for v in values.values()), values
    receipt['native_thumbnail_values'] = values
    receipt['interactive_cpu_counters'] = {key: max(row[key] for row in proof['captures'])
                                          for key in ('cpu_materializations', 'cpu_layer_compositions')}
    receipt['first_application'] = proof['checks'][0]
    receipt['apply_handler_max_ms'] = max(row['handler_return_ms'] for row in proof['checks'])
    receipt['property_handler_max_ms'] = max(row['handler_ms'] for row in proof['property_latency'])
    for phase in ('smart_mask_thumbnail_startup', 'smart_mask_thumbnail_tick'):
        timings = [float(v) for v in re.findall('phase=' + phase + r' wall_ms=([\d.]+)', log)]
        receipt[phase] = {'logged_calls_at_least_1ms': len(timings), 'maximum_ms': max(timings, default=0)}
    assert (out / 'smart-masks.blend').is_file()
    assert (out / 'smart-masks.blend.smart-mask-witness.json').is_file()
else:
    proof = json.loads((out / 'smart-mask-cold-open.json').read_text())
    assert proof['bindings'] == 16 and proof['shared_image_storages'] == 8
    assert proof['packed_source_hashes'] == 8 and proof['packed_refs_pixels_parameters']
    assert proof['rcc_decodes_before'] == proof['rcc_decodes_after_open'] == proof['rcc_decodes_after_gpu'] == 0
    assert 'BTS_SMART_MASK_COLD_OPEN_PASS' in log
(out / 'receipt.json').write_text(json.dumps(receipt, indent=2) + '\n')
print(json.dumps(receipt, indent=2))
