# SPDX-License-Identifier: GPL-3.0-or-later
"""Run the shipped PD149 uniform UI actor on an already-built matching host."""
import argparse
import hashlib
import importlib.util
import json
import math
import os
from pathlib import Path
import re
import signal
import subprocess
import sys
import time

def valid_parameter_witness(packet, cold):
    """Reject missing/stale actor packets as well as every numeric negative."""
    error = packet.get('maximum_mask_error')
    return bool(packet.get('observed') is True and packet.get('cold') is cold
        and type(packet.get('decoded_mask_components')) is int and packet['decoded_mask_components'] == 393216
        and type(packet.get('nonuniform_destination_tiles')) is int and packet['nonuniform_destination_tiles'] == 2
        and packet.get('fixed_tolerance') == 3e-5
        and type(error) in (int, float) and math.isfinite(error) and 0 <= error < 3e-5
        and type(packet.get('unique_payload_files')) is int and packet['unique_payload_files'] > 0
        and type(packet.get('source_members_version')) is int and packet['source_members_version'] == 3
        and (packet.get('original_source_hidden') is True if cold
             else packet.get('cancel_retired_without_project_mutation') is True))

def valid_parameter_distinct_tiles(packet):
    """The new roundtrip must detect tile swaps and tile1001 fallback."""
    differences = [packet.get('mask0_tile_maximum_difference'),
                   packet.get('reference_mask0_tile_maximum_difference')]
    return bool(packet.get('distinct_tile_masks') is True
        and all(type(value) in (int, float) and math.isfinite(value) and .1 < value <= 1
                for value in differences))

def valid_parameter_roundtrip_witness(packet, cold):
    error = packet.get('mask_maximum_error')
    required = ['actual_full_document_publication', 'one_history_commit', 'catalog_originals_once',
        'fresh_shader_assignment', 'native_shader_parameters', 'unrelated_sets_unchanged',
        'stale_result_rejected', 'owner_admitted_before_worker_decode',
        'catalog_metadata_preflight', 'missing_filename_rejected',
        'ordinary_layer_undo_retains_shader', 'undo_exact_document_and_library',
        'redo_exact_document_and_library', 'original_source_hidden']
    return bool(packet.get('observed') is True and packet.get('cold') is cold
        and type(packet.get('mask_components')) is int and packet['mask_components'] == 98304
        and packet.get('local_mask_slots') == [1, 2, 3]
        and all(type(slot) is int for slot in packet['local_mask_slots'])
        and packet.get('fixed_tolerance') == 3e-5
        and type(error) in (int, float) and math.isfinite(error) and 0 <= error < 3e-5
        and type(packet.get('decoder_calls')) is int and packet['decoder_calls'] > 0
        and type(packet.get('catalog_original_rows')) is int and packet['catalog_original_rows'] > 0
        and packet.get('package_hidden') is cold and valid_parameter_distinct_tiles(packet)
        and all(packet.get(key) is True for key in required))

def valid_parameter_import_witness(packet, cold):
    """Actual product UI/lifecycle evidence; no diagnostic-publisher substitute."""
    required = ['actual_import_button_and_directory', 'folder_cancel_unchanged',
        'actual_native_surface_rejected_before_worker', 'worker_cancel_retired_unchanged',
        'preparing_cancel_releases_owner_unchanged', 'completed_legacy_without_cancel',
        'actual_full_document_publication', 'one_history_commit', 'catalog_originals_once',
        'fresh_shader_assignment', 'native_shader_parameters', 'unrelated_sets_unchanged',
        'stale_result_rejected', 'ordinary_layer_undo_retains_shader',
        'undo_exact_document_and_library', 'redo_exact_document_and_library',
        'original_source_hidden']
    error = packet.get('mask_maximum_error')
    return bool(packet.get('observed') is True and packet.get('cold') is cold
        and type(packet.get('mask_components')) is int and packet['mask_components'] == 98304
        and packet.get('local_mask_slots') == [1, 2, 3]
        and all(type(slot) is int for slot in packet['local_mask_slots'])
        and packet.get('fixed_tolerance') == 3e-5
        and type(error) in (int, float) and math.isfinite(error) and 0 <= error < 3e-5
        and type(packet.get('catalog_original_rows')) is int and packet['catalog_original_rows'] == 4
        and packet.get('package_hidden') is cold and valid_parameter_distinct_tiles(packet)
        and all(packet.get(key) is True for key in required))

def valid_color_udim_witness(packet, cold, product_import=False):
    """Actual per-member source/stock/native proof; no startup/perf acceptance."""
    finite = lambda x: type(x) in (int, float) and math.isfinite(x)
    bounded = lambda x: finite(x) and 0 <= x < 3e-5
    if not isinstance(packet, dict) or packet.get('native_run_pass') is not True or packet.get('cold') is not cold:
        return False
    rows = packet.get('captures', [])
    labels = ['source-package-hidden-cold'] if cold else ['file-shelf','srt','undo','redo','reconstructed','reconstruction-redo']
    live = [row for row in rows if row.get('stock') is False]
    stock_rows = [(row.get('stage'),row.get('private_rec709_matrix')) for row in rows if row.get('stock') is True]
    expected_stock_rows = ([('cold-resaved-stock',False),('stock-private-rec709',True)] if cold else
        [('writer-stock',False),('stock-private-rec709',True),('reconstructed-stock',False),('stock-private-rec709',True)])
    if len(rows) != (3 if cold else 10) or stock_rows != expected_stock_rows:
        return False
    if [row.get('stage') for row in live] != labels or not all(
        type(row.get('stock')) is bool and type(row.get('private_rec709_matrix')) is bool
        and row.get('components') == 128 and row.get('finite') is True
        and type(row.get('nonfinite_components')) is int and row['nonfinite_components'] == 0
        and row.get('fixed_tolerance') == 3e-5 and bounded(row.get('maximum_error'))
        and bounded(row.get('mask_plane_maximum_error')) and bounded(row.get('restoration_error'))
        and isinstance(row.get('worst'), dict) and bool(row['worst'])
        and row.get('nonfinite_witness') == {} and len(row['worst'].get('actual_rgba',[])) == 4
        and all(finite(v) for v in row['worst']['actual_rgba'])
        and bounded(row['worst'].get('absolute_error')) and len(row.get('probes', [])) == 32
        and all(p.get('tile') in (1001,1002) and len(p.get('actual_color_metal',[])) == 4
            and len(p.get('independent',[])) == 4 and len(p.get('observed_masks',[])) == 3
            and all(finite(v) for v in p['actual_color_metal']+p['independent']+p['observed_masks']) for p in row['probes'])
        for row in rows):
        return False
    stock = packet.get('stock', [])
    if [row.get('stage') for row in stock] != (['cold-resaved-stock'] if cold else ['writer-stock','reconstructed-stock']) or not all(
        row.get('original_payloads') == 2 and type(row.get('runtime_aliases')) is int and row['runtime_aliases'] == 0
        and row.get('native_restore_called') is False and bounded(row.get('maximum_error'))
        and bounded(row.get('private709_maximum_error')) for row in stock):
        return False
    package, masks, reconstruction = (packet.get(k,{}) for k in ('package','reconstructed_masks','reconstruction'))
    if not (packet.get('actual_distinct_member_tags') is True and packet.get('originals_hidden') is True
        and package.get('version') == 2 and package.get('source_members_version') == 4
        and package.get('mask_components') == 98304 and bounded(package.get('mask_maximum_error'))
        and type(package.get('payload_files')) is int and package['payload_files'] >= 2
        and masks.get('components') == 98304 and masks.get('fixed_tolerance') == 3e-5 and bounded(masks.get('maximum_error'))
        and reconstruction.get('native_target_before_decode') is True
        and reconstruction.get('stale_publication_rejected') is True
        and (product_import or (reconstruction.get('linear_member_admissions') == 3
            and type(reconstruction.get('decoder_calls')) is int and reconstruction['decoder_calls'] > 0))
        and reconstruction.get('one_history_commit') is True and reconstruction.get('undo_redo_exact') is True):
        return False
    if product_import:
        required = ['folder_cancel_unchanged','native_surface_before_worker',
            'worker_cancel_retired_unchanged','stale_result_rejected',
            'actual_import_button_and_directory','one_history_commit','catalog_originals_once']
        evidence = packet.get('product_import', {})
        if packet.get('product_import_mode') is not True or not all(evidence.get(key) is True for key in required):
            return False
    if cold:
        return packet.get('package_hidden') is True
    sharing = [packet.get(k,{}) for k in ('shared_inputs','shared_two_sets')]
    return bool(packet.get('cancel_worker_pending_observed') is True
        and packet.get('cancel_fingerprint_unchanged') is True and packet.get('file_apply_one_undo') is True
        and packet.get('srt_stable_handles_counts') is True and packet.get('missing_footprint_atomic') is True
        and packet.get('export_cancel_unchanged') is True and packet.get('package_hidden') is False
        and all(v.get('unique_live_images') == 4 and v.get('unique_live_textures') == 4 for v in sharing)
        and all(packet.get('srt_upload_before',{}).get(k) == packet.get('srt_upload_after',{}).get(k)
            and type(packet.get('srt_upload_before',{}).get(k)) is int for k in ('calls','bytes')))

sys.dont_write_bytecode = True
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--profile', choices=['uniform', 'maps', 'maps-srgb', 'maps-bump', 'maps-udim', 'parameters', 'parameter-roundtrip', 'parameter-import', 'maps-color-udim'], default='uniform')
p.add_argument('--host', type=Path, required=True)
p.add_argument('--output', type=Path, required=True, help='Fresh directory under ../build')
p.add_argument('--environment', type=Path, required=True, help='Approved HDR fixture')
p.add_argument('--color-package-import', action='store_true', help='Color profile: use actual product Import instead of diagnostic publication')
a = p.parse_args()
if a.color_package_import and a.profile != 'maps-color-udim':
    p.error('--color-package-import requires --profile maps-color-udim')
repo = Path(__file__).resolve().parents[2]
expected_build = repo.parent / 'build'
host, out, environment = a.host.resolve(), a.output.resolve(), a.environment.resolve()
# Bind aliases can have different resolved spellings but the same directory inode.
# resolve() first rejects a symlink escape; require an actual matching ancestor.
build = next((parent for parent in out.parents
    if parent.exists() and parent.samefile(expected_build)), None)
if build is None:
    raise ValueError('Use a fresh private directory under build')
out.mkdir(parents=True, exist_ok=False)

def sha(path):
    h = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            h.update(block)
    return h.hexdigest()

def write(path, value):
    path.write_text(json.dumps(value, indent=2) + '\n')

fixture_source = repo / 'native/tests/pd149_uniform_fixture.py'
spec = importlib.util.spec_from_file_location('pd149_fixture', fixture_source)
fixture = importlib.util.module_from_spec(spec)
spec.loader.exec_module(fixture)
mesh = fixture.write_fixture(out / 'fixture', build)
if a.profile in ['parameters', 'parameter-roundtrip', 'parameter-import', 'maps-color-udim']:
    # The OBJ importer maps Ks directly to retained Specular IOR Level. Export
    # intentionally rejects noncanonical retained sockets rather than dropping
    # them; keep other profiles' original fixture byte-for-byte unchanged.
    mtl = mesh.with_suffix('.mtl')
    text = mtl.read_text()
    if text.count('Ks 0 0 0\n') != 3:
        raise ValueError('Parameter fixture expected three explicit OBJ specular entries')
    mtl.write_text(text.replace('Ks 0 0 0\n', 'Ks 0.5 0.5 0.5\n'))

source = (out / 'fixture' / ('bump-source.1001.exr' if a.profile == 'maps-udim' else 'bump-source.exr')) if a.profile in ['maps-bump', 'maps-udim'] else (fixture.write_map_source(out / 'fixture') if a.profile in ['maps', 'maps-srgb', 'parameters', 'parameter-roundtrip', 'parameter-import'] else None)
if a.profile == 'maps-color-udim':
    source = out / 'fixture' / 'color-udim-source.1001.exr'
output = out / 'output'
output.mkdir()
run_host = repo / 'scripts/host/run-host.sh'
guards = {str(path): sha(path) for path in [host, environment, fixture_source, mesh,
    mesh.with_suffix('.mtl'), Path(__file__), run_host,
    repo / 'scripts/host/gpu-env.sh', repo / 'scripts/host/session_logging.py']}
env = {k: v for k, v in os.environ.items() if not k.startswith(('BTS_', 'XDG_', 'QT_'))}
env.pop('WAYLAND_DISPLAY', None)
if not env.get('DISPLAY'):
    raise ValueError('Inherit the proven offscreen GLX DISPLAY/XAUTHORITY')
env.update(BTS_ROOT=str(build.parent), BTS_HOST_EXECUTABLE=str(host),
    BTS_ENVIRONMENT=str(environment), BTS_HEADLESS_WORKSPACE='1',
    BTS_GPU_BRUSH_AUTHORING='1', BTS_PERFORMANCE_PROFILE='1', BTS_SESSION_PROFILE='1',
    BTS_PD149_UV_SCALAR_TRACE='1',
    QT_QPA_PLATFORM='offscreen', LANG='sv_SE.UTF-8', LC_ALL='sv_SE.UTF-8',
    PYTHONDONTWRITEBYTECODE='1', LD_LIBRARY_PATH=str(host.parent / 'lib'))
for key, name in [('XDG_CONFIG_HOME', 'config'), ('XDG_DATA_HOME', 'data'),
    ('XDG_CACHE_HOME', 'cache'), ('XDG_STATE_HOME', 'state'), ('XDG_RUNTIME_DIR', 'runtime'),
    ('TMPDIR', 'tmp'), ('BTS_RECOVERY_DIR', 'recovery'), ('BTS_RESOURCE_CACHE', 'resources'),
    ('BTS_SESSION_LOG_DIR', 'logs'), ('BTS_ASSET_LIBRARY_DIR', 'library'),
    ('BTS_TEMPLATE_DIR', 'templates'), ('CUDA_CACHE_PATH', 'cuda-cache'),
    ('__GL_SHADER_DISK_CACHE_PATH', 'gl-cache')]:
    directory = out / name
    directory.mkdir(mode=0o700)
    env[key] = str(directory)
env['__GL_SHADER_DISK_CACHE'] = '1'
command = ['bash', str(run_host), '--no-splash', '--workspace',
    '--material-layering-demo', str(output), '--gui-fix-mesh', str(mesh),
    '--gui-fix-resolution', '128', '--autosave-seconds', '3600',
    '--library-dir', str(out / 'library')]
if a.profile in ['parameters', 'parameter-roundtrip', 'parameter-import']:
    env.update(BTS_PD149_PARAMETER_PACKAGE=str(output / 'materials.btsparams'),
        BTS_PD149_PARAMETER_REFERENCE=str(output / 'mask-reference.f32'))
if a.profile in ['parameter-roundtrip', 'parameter-import']:
    env['BTS_PD149_PARAMETER_ROUNDTRIP_PRODUCER'] = '1'
if a.profile == 'maps-color-udim':
    env.update(BTS_PD149_COLOR_UDIM_SOURCE=str(source),
        BTS_PD149_COLOR_UDIM_PROJECT=str(output / 'mapped.blend'),
        BTS_PD149_COLOR_UDIM_PACKAGE=str(output / 'materials.btsparams'))
env.pop('BTS_PD149_COLOR_PACKAGE_IMPORT', None)
if a.color_package_import:
    env['BTS_PD149_COLOR_PACKAGE_IMPORT'] = '1'
if a.profile == 'maps-srgb':
    env['BTS_PD149_MAP_SRGB'] = '1'
if a.profile in ['maps-bump', 'maps-udim']:
    env['BTS_PD149_BUMP_SOURCE'] = '1'
if a.profile == 'maps-udim':
    env['BTS_PD149_MAP_UDIM'] = '1'
if source and a.profile != 'maps-color-udim':
    env.update(BTS_PD149_MAP_SOURCE=str(source), BTS_PD149_MAP_PROJECT=str(output / 'mapped.blend'))
    if source.is_file():
        guards[str(source)] = sha(source)
write(out / 'admission.json', {'scope': 'correctness-only shipped actor; no build or adapted main',
    'command': command, 'xauthority_present': bool(env.get('XAUTHORITY')),
    'xauthority_value_sha256': hashlib.sha256(env['XAUTHORITY'].encode()).hexdigest() if env.get('XAUTHORITY') else None,
    'environment': {k: v for k, v in env.items()
        if k != 'XAUTHORITY' and (k.startswith(('BTS_', 'XDG_', 'QT_')) or k in ['DISPLAY',
            'TMPDIR', 'LANG', 'LC_ALL', 'CUDA_CACHE_PATH', '__GL_SHADER_DISK_CACHE_PATH',
            '__GL_SHADER_DISK_CACHE', 'LD_LIBRARY_PATH'])}, 'inputs_sha256': guards})
result = {'actual_exit': 1, 'native_exit': None, 'profile': a.profile, 'phases': []}

start = time.monotonic()
try:
    for path, expected in guards.items():
        if sha(path) != expected:
            raise RuntimeError('Input drift: ' + path)
    def run_phase(name, phase_out, phase_output, phase_env, phase_command):
        phase = {'name': name}
        result['phases'].append(phase)
        with (phase_out / 'run.log').open('w') as log:
            process = subprocess.Popen(['nice', '-n', '19', *phase_command],
                cwd=phase_out, env=phase_env, stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT, start_new_session=True)
            phase['native_pid'] = process.pid
            try:
                captured, _ = process.communicate(timeout=600)
            except BaseException:
                try:
                    os.killpg(process.pid, signal.SIGTERM)
                except ProcessLookupError:
                    pass
                try:
                    captured, _ = process.communicate(timeout=5)
                except subprocess.TimeoutExpired:
                    os.killpg(process.pid, signal.SIGKILL)
                    captured, _ = process.communicate()
                log.write(captured.decode(errors='replace'))
                phase['native_exit'] = process.returncode
                raise
            log.write(captured.decode(errors='replace'))
        phase['native_exit'] = process.returncode
        result['native_exit'] = process.returncode
        text = (phase_out / 'run.log').read_text(errors='replace')
        clean = re.findall(r'\bBTS_SHUTDOWN clean pid=(\d+)\b', text)
        phase['clean_pid_matches'] = clean == [str(process.pid)]
        phase['native_actor_pass'] = ('PD149_COLOR_UDIM_UI PASS' if a.profile == 'maps-color-udim' else 'PD149_BUMP_UI PASS' if a.profile in ['maps-bump', 'maps-udim'] else 'PD149_MAP_UI PASS' if a.profile in ['maps', 'maps-srgb', 'parameters', 'parameter-roundtrip', 'parameter-import'] else 'PD149_UNIFORM_UI PASS') in text
        renderer = re.findall(r'\bBTS_GPU_READY pid=' + str(process.pid) + r' renderer=(.*?) qt=', text)
        phase['gpu_renderer'] = renderer[-1] if renderer else None
        phase['hardware_renderer'] = bool(renderer) and not any(
            name in renderer[-1].lower() for name in ('llvmpipe', 'lavapipe', 'software rasterizer'))
        phase['sampler_model_supported'] = a.profile not in ['maps', 'maps-srgb', 'maps-bump', 'maps-udim', 'parameters', 'parameter-roundtrip', 'parameter-import', 'maps-color-udim'] or bool(renderer and 'NVIDIA GeForce RTX 3090' in renderer[-1])
        phase['unmapped_native_window'] = bool(re.search(
            r'\bBTS_SCENE_READY[^\n]*native_window=none\b', text))
        phase['forbidden'] = re.findall(r'^.*(?:BTS_HOST_FAILED|BTS_DRAW_FAILED|'
            r'BTS_\w*CLEANUP_FAILED|PD149_(?:UNIFORM|MAP|BUMP|COLOR_UDIM)_UI FAILED|Shader compile failed:|'
            r'BTS_FINALIZE_MS|phase=padding\b|BTS_GPU_CPU_REFERENCE|Segmentation fault|'
            r'ID user (?:count|decrement) error|AddressSanitizer|Not freed memory blocks).*$', text, re.M)
        proof_path = phase_output / ('pd149-color-udim.json' if a.profile == 'maps-color-udim' else 'pd149-bump-artist.json' if a.profile in ['maps-bump', 'maps-udim'] else 'pd149-map-artist.json' if a.profile in ['maps', 'maps-srgb', 'parameters', 'parameter-roundtrip', 'parameter-import'] else 'pd149-uniform-artist.json')
        phase['proof_present'] = proof_path.is_file()
        proof = json.loads(proof_path.read_text()) if phase['proof_present'] else {}
        phase['color_udim_witness_valid'] = a.profile != 'maps-color-udim' or valid_color_udim_witness(proof, name == 'source-hidden-cold', a.color_package_import)
        phase['stock_witness_valid'] = True
        if a.profile == 'maps-srgb' and name == 'writer':
            stock = proof.get('stock', {})
            errors = [stock.get('maximum_error'), stock.get('runtime_delta')]
            phase['stock_witness_valid'] = bool(stock and stock.get('observed') is True
                and stock.get('components') == 192 and len(stock.get('probes', [])) == 32
                and stock.get('original_payloads') == 1 and stock.get('runtime_aliases') == 0
                and all(isinstance(x, (int, float)) and math.isfinite(x) and 0 <= x < 3e-5
                    for x in errors))
        phase['bump_witness_valid'] = True
        if a.profile in ['maps-bump', 'maps-udim']:
            captures, fragments = proof.get('captures', []), proof.get('fragments', [])
            expected_labels = (['source-hidden-cold'] if name == 'source-hidden-cold' else
                ['neutral', 'file', 'undo', 'redo', 'shelf', 'srt', 'cleared', 'restored', 'saved'])
            expected_fragments = (['source-hidden-cold'] if name == 'source-hidden-cold' else
                ['neutral', 'file', 'undo-neutral', 'redo', 'srt', 'cleared-neutral', 'restored'])
            finite = lambda value: isinstance(value, (int, float)) and math.isfinite(value)
            phase['bump_witness_valid'] = bool(
                [row.get('stage') for row in captures] == expected_labels
                and [row.get('stage') for row in fragments] == expected_fragments
                and all(row.get('components') == 32 and row.get('fixed_tolerance') == 3e-5
                    and finite(row.get('maximum_error')) and 0 <= row['maximum_error'] < 3e-5
                    and len(row.get('probes', [])) == 32 for row in captures)
                and all(row.get('scene_only') is True and row.get('fragment_not_compute') is True
                    and row.get('components') == 54 and row.get('fixed_rgb8_bound') == 2
                    and row.get('surface_layout_document_restored') is True
                    and row.get('numeric_comparison_performed') is True
                    and row.get('budget_precondition') is True and row.get('nonflat_signal_precondition') is True
                    and finite(row.get('maximum_rgb8_error_codes')) and 0 <= row['maximum_rgb8_error_codes'] <= 2
                    and len(row.get('rows', [])) == 2
                    and {p.get('tile') for p in row['rows']} == {1001, 1002}
                    and all(finite(p.get('derived_bound_codes')) and 0 <= p['derived_bound_codes'] <= 2
                        and len(p.get('pixels', [])) == 9
                        and all(len(pixel.get('actual_rgb8', [])) == 3
                            and all(finite(c) and 0 <= c <= 255 for c in pixel['actual_rgb8']) for pixel in p['pixels'])
                        and (row['stage'] in ['neutral', 'undo-neutral', 'cleared-neutral']
                            or (finite(p.get('signal_codes')) and p['signal_codes'] > 4)) for p in row['rows'])
                    for row in fragments))
        phase['udim_witness_valid'] = True
        if a.profile == 'maps-udim':
            scalars = proof.get('udim_scalars', [])
            finite = lambda value: isinstance(value, (int, float)) and math.isfinite(value)
            phase['udim_witness_valid'] = bool(proof.get('raw_udim') is True
                and [row.get('stage') for row in scalars] == expected_labels
                and all(row.get('components') == 96 and row.get('fixed_tolerance') == 3e-5
                    and len(row.get('probes', [])) == 32 and finite(row.get('maximum_error'))
                    and 0 <= row['maximum_error'] < 3e-5 for row in scalars))
            if name in ['writer', 'source-hidden-cold']:
                stock = proof.get('udim_stock', {})
                phase['udim_witness_valid'] &= bool(stock.get('observed') is True
                    and stock.get('components') == 128 and len(stock.get('probes', [])) == 128
                    and stock.get('original_payloads') == 2 and stock.get('runtime_aliases') == 0
                    and stock.get('native_restore_called') is False
                    and finite(stock.get('maximum_error')) and 0 <= stock['maximum_error'] < 3e-5
                    and finite(stock.get('runtime_delta')) and 0 <= stock['runtime_delta'] < 3e-5)
        phase['parameter_witness_valid'] = (a.profile not in ['parameters', 'parameter-roundtrip', 'parameter-import']
            or (a.profile in ['parameter-roundtrip', 'parameter-import'] and name != 'writer')
            or valid_parameter_witness(proof.get('parameter_package', {}), name == 'source-hidden-cold'))
        if a.profile in ['parameter-roundtrip', 'parameter-import'] and name == 'writer':
            phase['parameter_witness_valid'] &= valid_parameter_distinct_tiles(proof.get('parameter_package', {}))
        phase['roundtrip_witness_valid'] = True
        if a.profile in ['parameter-roundtrip', 'parameter-import'] and name != 'writer':
            rows = proof.get('captures', [])
            labels = (['source-package-hidden-cold'] if name == 'source-hidden-cold'
                else ['reconstructed', 'ordinary-layer-undo', 'redo', 'saved'])
            phase['roundtrip_witness_valid'] = bool(
                (valid_parameter_import_witness if a.profile == 'parameter-import' else valid_parameter_roundtrip_witness)(proof.get('parameter_reconstruction', {}), name == 'source-hidden-cold')
                and [row.get('stage') for row in rows] == labels
                and all(row.get('components') == 192 and row.get('fixed_tolerance') == 3e-5
                    and len(row.get('probes', [])) == 32 and isinstance(row.get('maximum_error'), (int, float))
                    and math.isfinite(row['maximum_error']) and 0 <= row['maximum_error'] < 3e-5 for row in rows))
        if not phase['color_udim_witness_valid'] or phase['native_exit'] != 0 or not phase['clean_pid_matches'] or not phase['native_actor_pass'] or not phase['hardware_renderer'] or not phase['unmapped_native_window'] or not phase['sampler_model_supported'] or phase['forbidden'] or proof.get('native_run_pass') is not True or not phase['stock_witness_valid'] or not phase['bump_witness_valid'] or not phase['udim_witness_valid'] or not phase['parameter_witness_valid'] or not phase['roundtrip_witness_valid']:
            raise RuntimeError('First native actor/shutdown negative retained')
        return phase
    run_phase('writer' if source else 'uniform', out, output, env, command)
    if a.profile == 'maps-color-udim':
        writer = json.loads((output / 'pd149-color-udim.json').read_text())
        for tile in (1001,1002):
            path = source.with_name(source.name.replace('1001.exr',str(tile)+'.exr'))
            hidden = Path(str(path)+'.hidden')
            if path.exists() or not hidden.is_file():
                raise RuntimeError('Color writer must retain both hidden original fixtures')
            guards[str(hidden)] = sha(hidden)
        begin, end = 'PD149_COLOR_UDIM_SRT_BEGIN','PD149_COLOR_UDIM_SRT_END'
        log = (out / 'run.log').read_text(errors='replace')
        if log.count(begin) != 1 or log.count(end) != 1 or log.index(begin) >= log.index(end):
            raise RuntimeError('Color SRT source-allocation observation window missing')
        window = log.split(begin,1)[1].split(end,1)[0]
        if any(marker in window for marker in ('BTS_GPU_RESOURCE_ALLOC','BTS_TEXTURE_LOAD','BTS_TEXTURE_SHARED')):
            raise RuntimeError('Color SRT unexpectedly decoded/allocated a source')
        result['srt_source_allocation_log_delta'] = 0
        project, expected, masks = (output / name for name in ('mapped.blend','color-udim-expected.json','color-udim-mask-reference.f32'))
        package = output / 'materials.btsparams'
        for path in (project,expected,masks):
            if not path.is_file():
                raise RuntimeError('Color writer artifact missing: '+str(path))
            guards[str(path)] = sha(path)
        for path in package.rglob('*'):
            if path.is_file():
                guards[str(path)] = sha(path)
        hidden_package = package.with_suffix('.package-hidden')
        package.rename(hidden_package)
        for path in list(guards):
            original = Path(path)
            if original.is_relative_to(package):
                guards[str(hidden_package / original.relative_to(package))] = guards.pop(path)
        cold = out / 'cold'
        cold.mkdir()
        cold_output = cold / 'output'
        cold_output.mkdir()
        cold_env = dict(env, BTS_PD149_COLOR_UDIM_COLD='1',
            BTS_PD149_COLOR_UDIM_EXPECTED=str(expected),BTS_PD149_COLOR_UDIM_MASK_REFERENCE=str(masks))
        for key in ['XDG_CONFIG_HOME','XDG_DATA_HOME','XDG_CACHE_HOME','XDG_STATE_HOME',
                    'XDG_RUNTIME_DIR','TMPDIR','BTS_RECOVERY_DIR','BTS_RESOURCE_CACHE',
                    'BTS_SESSION_LOG_DIR','BTS_ASSET_LIBRARY_DIR','BTS_TEMPLATE_DIR',
                    'CUDA_CACHE_PATH','__GL_SHADER_DISK_CACHE_PATH']:
            directory = cold / Path(env[key]).name
            directory.mkdir(mode=0o700)
            cold_env[key] = str(directory)
        cold_command = list(command)
        cold_command[cold_command.index('--material-layering-demo')+1] = str(cold_output)
        cold_command[cold_command.index('--library-dir')+1] = cold_env['BTS_ASSET_LIBRARY_DIR']
        write(cold / 'admission.json', {'command':cold_command,'project_sha256':sha(project),
            'originals_hidden':True,'package_hidden':not package.exists(),
            'scope':'fresh private roots; Color UDIM ideal3e-5; no latency claim'})
        run_phase('source-hidden-cold',cold,cold_output,cold_env,cold_command)
        reopened = json.loads((cold_output / 'pd149-color-udim.json').read_text())
        before = next(row for row in writer['captures'] if row['stage']=='reconstruction-redo')
        after = next(row for row in reopened['captures'] if row['stage']=='source-package-hidden-cold')
        maximum = 0.0
        if len(before['probes']) != 32 or len(after['probes']) != 32:
            raise RuntimeError('Color saved/cold witness shape')
        for old,new in zip(before['probes'],after['probes']):
            if old['tile']!=new['tile'] or old['xy']!=new['xy']:
                raise RuntimeError('Color saved/cold witness position')
            for x,y in zip(old['actual_color_metal'],new['actual_color_metal']):
                if not all(type(v) in (int,float) and math.isfinite(v) for v in (x,y)):
                    raise RuntimeError('Color saved/cold nonfinite')
                maximum=max(maximum,abs(x-y))
        result.update(saved_cold_components=128,saved_cold_maximum_error=maximum)
        if maximum >= 1e-6:
            raise RuntimeError('Color saved/cold GPU values changed')
        source = None  # This profile has completed its own bounded two-process pair.
    if source:
        if not source.is_file():
            raise RuntimeError('Writer did not retain its original source fixture')
        guards[str(source)] = sha(source)
        project = output / 'mapped.blend'
        expected = output / 'expected-materials.json'
        if not project.is_file() or not expected.is_file():
            raise RuntimeError('Writer did not retain packed project/expected inputs')
        guards[str(project)] = sha(project)
        guards[str(expected)] = sha(expected)
        if a.profile == 'maps-udim':
            other_source = source.with_name(source.name.replace('.1001.', '.1002.'))
            if not other_source.is_file():
                raise RuntimeError('Writer did not retain the second original UDIM file')
            other_hidden = other_source.with_suffix('.source-hidden')
            guards[str(other_source)] = sha(other_source)
            other_source.rename(other_hidden)
            del guards[str(other_source)]
            guards[str(other_hidden)] = sha(other_hidden)
        if a.profile in ['parameters', 'parameter-roundtrip', 'parameter-import']:
            package = output / 'materials.btsparams'
            relocated = out / 'relocated-parameters.btsparams'
            package.rename(relocated)
            for path in relocated.rglob('*'):
                if path.is_file():
                    guards[str(path)] = sha(path)
            guards[str(output / 'mask-reference.f32')] = sha(output / 'mask-reference.f32')
            result['parameter_package'] = {'relocated_path': str(relocated),
                'mask_reference_sha256': sha(output / 'mask-reference.f32'),
                'files_sha256': {str(path.relative_to(relocated)): sha(path)
                    for path in relocated.rglob('*') if path.is_file()}}
        hidden = source.with_suffix('.source-hidden')
        source.rename(hidden)
        del guards[str(source)]
        guards[str(hidden)] = sha(hidden)
        comparison_output = output
        if a.profile in ['parameter-roundtrip', 'parameter-import']:
            native = out / 'reconstruction'
            native.mkdir()
            native_output = native / 'output'
            native_output.mkdir()
            native_env = dict(env, BTS_PD149_PARAMETER_RECONSTRUCT='1',
                BTS_PD149_PARAMETER_PACKAGE=str(relocated),
                BTS_PD149_MAP_PROJECT=str(native_output / 'mapped.blend'))
            native_env.pop('BTS_PD149_PARAMETER_ROUNDTRIP_PRODUCER', None)
            if a.profile == 'parameter-import':
                native_env['BTS_PD149_PARAMETER_IMPORT'] = '1'
            for key in ['XDG_CONFIG_HOME','XDG_DATA_HOME','XDG_CACHE_HOME','XDG_STATE_HOME',
                        'XDG_RUNTIME_DIR','TMPDIR','BTS_RECOVERY_DIR','BTS_RESOURCE_CACHE',
                        'BTS_SESSION_LOG_DIR','BTS_ASSET_LIBRARY_DIR','BTS_TEMPLATE_DIR',
                        'CUDA_CACHE_PATH','__GL_SHADER_DISK_CACHE_PATH']:
                directory = native / Path(env[key]).name
                directory.mkdir(mode=0o700)
                native_env[key] = str(directory)
            native_command = list(command)
            native_command[native_command.index('--material-layering-demo')+1] = str(native_output)
            native_command[native_command.index('--library-dir')+1] = native_env['BTS_ASSET_LIBRARY_DIR']
            write(native / 'admission.json', {'command': native_command, 'source_hidden': not source.exists(),
                'package_manifest_sha256': sha(relocated / 'parameters.json'),
                'scope': ('real Import button/folder/worker/scoped preparation/full-document publication; no latency claim' if a.profile == 'parameter-import' else 'real New + owner admission/worker decode + full-document native reconstruction; no Import UI or latency claim')})
            run_phase('native-reconstruction', native, native_output, native_env, native_command)
            producer = json.loads((output / 'pd149-map-artist.json').read_text())
            restored = json.loads((native_output / 'pd149-map-artist.json').read_text())
            maximum = 0.0
            for before, after in zip(producer['captures'][-1]['probes'], restored['captures'][-1]['probes']):
                if before['tile'] != after['tile'] or before['xy'] != after['xy'] or len(before['actual']) != 6 or len(after['actual']) != 6:
                    raise RuntimeError('Original/reconstructed AOV position or shape differs')
                for x, y in zip(before['actual'], after['actual']):
                    if not math.isfinite(x) or not math.isfinite(y):
                        raise RuntimeError('Original/reconstructed AOV is nonfinite')
                    maximum = max(maximum, abs(x-y))
            if len(producer['captures'][-1]['probes']) != 32 or len(restored['captures'][-1]['probes']) != 32 or maximum >= 3e-5:
                raise RuntimeError('Native reconstruction differs from original 192 AOV components')
            result['original_reconstructed_components'] = 192
            result['original_reconstructed_maximum_error'] = maximum
            package_hidden = relocated.with_suffix('.package-hidden')
            package_files = {path: sha(path) for path in relocated.rglob('*') if path.is_file()}
            relocated.rename(package_hidden)
            for path, digest in package_files.items():
                del guards[str(path)]
                guards[str(package_hidden / path.relative_to(relocated))] = digest
            project = native_output / 'mapped.blend'
            expected = native_output / 'expected-materials.json'
            reconstruction_expected = native_output / 'roundtrip-expected.json'
            for path in [project, expected, reconstruction_expected]:
                guards[str(path)] = sha(path)
            comparison_output = native_output
        cold = out / 'cold'
        cold.mkdir()
        cold_output = cold / 'output'
        cold_output.mkdir()
        cold_env = dict(env, BTS_PD149_MAP_COLD='1', BTS_PD149_MAP_EXPECTED=str(expected))
        if a.profile in ['parameter-roundtrip', 'parameter-import']:
            cold_env.pop('BTS_PD149_PARAMETER_ROUNDTRIP_PRODUCER', None)
            cold_env.update(BTS_PD149_PARAMETER_RECONSTRUCT='1',
                BTS_PD149_MAP_PROJECT=str(project),
                BTS_PD149_PARAMETER_ROUNDTRIP_EXPECTED=str(reconstruction_expected))
            if a.profile == 'parameter-import':
                cold_env['BTS_PD149_PARAMETER_IMPORT'] = '1'

        if a.profile in ['parameters', 'parameter-roundtrip', 'parameter-import']:
            cold_env['BTS_PD149_PARAMETER_PACKAGE'] = str(relocated)
        for key in ['XDG_CONFIG_HOME','XDG_DATA_HOME','XDG_CACHE_HOME','XDG_STATE_HOME',
                    'XDG_RUNTIME_DIR','TMPDIR','BTS_RECOVERY_DIR','BTS_RESOURCE_CACHE',
                    'BTS_SESSION_LOG_DIR','BTS_ASSET_LIBRARY_DIR','BTS_TEMPLATE_DIR',
                    'CUDA_CACHE_PATH','__GL_SHADER_DISK_CACHE_PATH']:
            directory = cold / Path(env[key]).name
            directory.mkdir(mode=0o700)
            cold_env[key] = str(directory)
        if source.exists():
            raise RuntimeError('Cold original must be absent')
        cold_command = list(command)
        cold_command[cold_command.index('--material-layering-demo')+1] = str(cold_output)
        if a.profile in ['parameter-roundtrip', 'parameter-import']:
            cold_command[cold_command.index('--library-dir')+1] = cold_env['BTS_ASSET_LIBRARY_DIR']
        write(cold / 'admission.json', {'command': cold_command,
            'source_hidden': not source.exists(), 'project_sha256': sha(project),
            'expected_sha256': sha(expected), 'all_private_roots_initially_empty':
                all(not list(Path(cold_env[key]).iterdir()) for key in ['XDG_CONFIG_HOME',
                    'XDG_CACHE_HOME','BTS_RESOURCE_CACHE','CUDA_CACHE_PATH','__GL_SHADER_DISK_CACHE_PATH'])})
        run_phase('source-hidden-cold', cold, cold_output, cold_env, cold_command)
        proof_name = 'pd149-bump-artist.json' if a.profile in ['maps-bump', 'maps-udim'] else 'pd149-map-artist.json'
        writer_proof = json.loads((comparison_output / proof_name).read_text())
        cold_proof = json.loads((cold_output / proof_name).read_text())
        saved_rows = writer_proof['captures'][-1]['probes']
        cold_rows = cold_proof['captures'][-1]['probes']
        if len(saved_rows) != 32 or len(cold_rows) != 32:
            raise RuntimeError('Expected 32 two-tile saved/cold sample positions')
        maximum = 0.0
        for before, after in zip(saved_rows, cold_rows):
            if before['tile'] != after['tile'] or before['xy'] != after['xy'] or len(before['actual']) != (1 if a.profile in ['maps-bump', 'maps-udim'] else 6) or len(after['actual']) != (1 if a.profile in ['maps-bump', 'maps-udim'] else 6):
                raise RuntimeError('Saved/cold scalar packet shape differs')
            for x, y in zip(before['actual'], after['actual']):
                if not math.isfinite(x) or not math.isfinite(y):
                    raise RuntimeError('Saved/cold packet is nonfinite')
                maximum = max(maximum, abs(x-y))
        if a.profile == 'maps-udim':
            saved = writer_proof['udim_scalars'][-1]['probes']
            reopened = cold_proof['udim_scalars'][-1]['probes']
            if len(saved) != 32 or len(reopened) != 32:
                raise RuntimeError('UDIM scalar saved/cold rows are missing')
            for before, after in zip(saved, reopened):
                if before['tile'] != after['tile'] or before['xy'] != after['xy'] or len(before['actual']) != 3 or len(after['actual']) != 3:
                    raise RuntimeError('UDIM scalar saved/cold shape mismatch')
                for x, y in zip(before['actual'], after['actual']):
                    if not math.isfinite(x) or not math.isfinite(y):
                        raise RuntimeError('UDIM scalar cold value is nonfinite')
                    maximum = max(maximum, abs(x-y))
        result['saved_cold_components'] = 128 if a.profile == 'maps-udim' else 32 if a.profile in ['maps-bump', 'maps-udim'] else 192
        result['saved_cold_maximum_error'] = maximum
        if maximum >= 1e-6:
            raise RuntimeError('Packed cold Open did not restore saved GPU samples')
    result['actual_exit'] = 0
except Exception as error:
    result['error'] = repr(error)
finally:
    for path, expected in guards.items():
        if sha(path) != expected:
            result.update(actual_exit=1, input_drift=path)
    result['wall_seconds'] = time.monotonic() - start
    result['artifacts_sha256'] = {str(path): sha(path) for path in out.rglob('*')
        if path.is_file() and path.suffix in ['.json', '.png', '.log']}
    write(out / 'result.json', result)
print('PD149 ' + a.profile + ' UI', 'PASS' if result['actual_exit'] == 0 else 'FAILED',
    'native_exit=' + str(result['native_exit']), 'evidence=' + str(out / 'result.json'))
sys.exit(result['actual_exit'])
