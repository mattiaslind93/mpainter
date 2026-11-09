# SPDX-License-Identifier: GPL-3.0-or-later
"""Build/run the isolated real Wood bitmap mask native GPU and packed Open oracle.

The required snapshot contains host-commands.json and immutable local host link
inputs. Compile this test and the current source/bitmap/admission helpers; reuse tested async Open
objects through its compiled.json. Never read mutable artist .o/.a files.
Coordinate a GPU slot before omitting --build-only or using --run-only.
"""
import argparse
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--host-snapshot', type=Path, required=True)
p.add_argument('--async-objects', type=Path, required=True)
p.add_argument('--host-resources', type=Path, required=True, help='Stable private host directory containing the versioned SDK data tree')
p.add_argument('--output', type=Path, required=True)
p.add_argument('--fixture', type=Path, required=True)
p.add_argument('--build-only', action='store_true')
p.add_argument('--run-only', action='store_true')
a = p.parse_args()
repo = Path(__file__).resolve().parents[2]
bts = Path(os.environ.get('BTS_ROOT', repo.parent)).resolve()
snapshot, output, async_objects = a.host_snapshot.resolve(), a.output.resolve(), a.async_objects.resolve()
if not output.is_relative_to(bts / 'build') or output == bts / 'build' or output == snapshot or output.is_relative_to(snapshot) or output.is_relative_to(bts / 'build/host'):
    raise SystemExit('Output must be private BTS_ROOT/build storage, outside the immutable snapshot')
output.mkdir(parents=True, exist_ok=True)
(output / 'bin').mkdir(exist_ok=True)
(output / 'tmp').mkdir(exist_ok=True)
binary = output / 'bin/spsm-bitmap-mask-native-test'
env = dict(os.environ, CCACHE_DIR=str(bts / 'build/ccache'), TMPDIR=str(output / 'tmp'))
if not a.run_only:
    commands = json.loads((snapshot / 'host-commands.json').read_text())
    def compilation(name):
        return shlex.split(next(c for c in commands if ' -o ' in c and '/' + name + '.o -c ' in c))
    replacements = {key: value for key, value in json.loads((async_objects / 'compiled.json').read_text()).items()
                    if not key.startswith('__') and not key.endswith('/main.cc.o')
                    and not key.endswith('/SpsmSource.cc.o')}
    for obj in replacements.values():
        if not Path(obj).is_file():
            raise SystemExit('Missing immutable async object: ' + obj)
    # Header-only scalar parser is deliberately shared with both lowering paths.
    for name in ['SpsmSource.cc', 'SpsmBitmapLowering.cc', 'SpsmMeshMapLowering.cc', 'SpsmMaterialImport.cc', 'AssetCatalog.cc', 'SpsmAssetDispatch.cc', 'spsm_bitmap_mask_native_test.cc']:
        test = name.endswith('_test.cc')
        extra = not test and not any('/' + name + '.o -c ' in c for c in commands)
        args = compilation('MeshBaker.cc' if test else 'SpsmSource.cc' if extra else name)
        previous = args[args.index('-o') + 1]
        old_root = str(Path(args[args.index('-c') + 1]).parent)
        args = [arg.replace(old_root, str(repo / 'native/host')) for arg in args]
        obj = output / (name + '.o')
        args[args.index('-o') + 1] = str(obj)
        args[args.index('-c') + 1] = str(repo / ('native/tests' if test else 'native/host') / name)
        for option, value in [('-MF', str(obj) + '.d'), ('-MT', str(obj))]:
            if option in args:
                args[args.index(option) + 1] = value
        args = ['-O1' if arg in ('-O2', '-O3') else arg for arg in args]
        args.append('-UNDEBUG')
        print('Compile', name, flush=True)
        with (output / (name + '.compile.log')).open('w') as log:
            subprocess.run(['nice', '-n', '19', *args], cwd=snapshot, env=env,
                           stdout=log, stderr=subprocess.STDOUT, check=True)
        replacements['__test__' if test else '__extra_' + name if extra else previous] = str(obj)
    link = shlex.split(commands[-1])
    if link[:2] == [':', '&&']:
        link = link[2:]
    if '&&' in link:
        link = link[:link.index('&&')]
    link = [replacements.get(arg, arg) for arg in link if not arg.endswith('/main.cc.o')]
    first = next(i for i, arg in enumerate(link) if arg.endswith('.o'))
    link[first:first] = [value for key, value in replacements.items() if key.startswith('__')]
    link[link.index('-o') + 1] = str(binary)
    link = ['-Wl,--dependency-file=' + str(output / 'link.d') if arg.startswith('-Wl,--dependency-file=')
            else arg.replace(str(bts / 'build/host/bin/lib'), str(snapshot / 'bin/lib')) for arg in link]
    # Reject missing relative frozen inputs; never silently fall back to a live
    # host build while linking during the artist's independent rebuild.
    for arg in link:
        if arg.endswith(('.o', '.a')):
            path = Path(arg)
            if not path.is_absolute():
                path = snapshot / path
            if not path.is_file() or path.is_relative_to(bts / 'build/host'):
                raise SystemExit('Non-frozen/missing host link input: ' + str(path))
    print('Link isolated native GPU oracle', flush=True)
    with (output / 'link.log').open('w') as log:
        subprocess.run(['nice', '-n', '19', *link], cwd=snapshot, env=env,
                       stdout=log, stderr=subprocess.STDOUT, check=True)
    # SDK data is static; use the already deployed private host's resource tree.
    data_root = a.host_resources.resolve()
    for resource in data_root.iterdir():
        if resource.is_dir() and resource.name[0].isdigit() and not (binary.parent / resource.name).exists():
            (binary.parent / resource.name).symlink_to(resource, target_is_directory=True)
    (output / 'build.json').write_text(json.dumps({'source': str(repo), 'snapshot': str(snapshot),
        'async_objects': str(async_objects), 'replacements': replacements}, indent=2) + '\n')
if not a.build_only:
    env.update(BTS_ROOT=str(bts), BTS_HOST_EXECUTABLE=str(binary), QT_QPA_PLATFORM='offscreen', BTS_HEADLESS_WORKSPACE='1',
               LD_LIBRARY_PATH=str(snapshot / 'bin/lib') + ':' + str(a.host_resources.resolve() / 'lib') + (':' + env['LD_LIBRARY_PATH'] if env.get('LD_LIBRARY_PATH') else ''),
               BTS_ENVIRONMENT=env.get('BTS_ENVIRONMENT', str(bts / 'build/environment-cache/TT_environmentLights_v001.hdr')),
               XDG_CONFIG_HOME=str(output / 'config'), XDG_DATA_HOME=str(output / 'data'),
               XDG_CACHE_HOME=str(output / 'cache'), XDG_STATE_HOME=str(output / 'state'), BTS_RECOVERY_DIR=str(output / 'recovery'),
               BTS_SESSION_LOG_DIR=str(output / 'logs'), BTS_RESOURCE_CACHE=str(output / 'resources'))
    inputs = output / 'inputs'
    inputs.mkdir(exist_ok=True)
    fixture = inputs / 'used_varnish_wood.spsm'
    held = Path(str(fixture) + '.held')
    if held.exists():
        held.unlink()
    for name in ['first-cache', 'second-cache']:
        shutil.rmtree(output / name, ignore_errors=True)
    shutil.copy2(a.fixture.resolve(), fixture)
    print('Run isolated bitmap mask native GPU/source-free packed Open', flush=True)
    log_path = output / 'run.log'
    with log_path.open('w') as log:
        subprocess.run(['nice', '-n', '19', 'bash', str(repo / 'scripts/host/run-host.sh'), str(output), str(fixture)],
                       cwd=repo, env=env, stdout=log, stderr=subprocess.STDOUT, check=True, timeout=240)
    proof = json.loads((output / 'verified.json').read_text())
    log = (output / 'run.log').read_text()
    assert 'BTS_SHUTDOWN clean' in log and 'Not freed memory blocks' not in log
    assert proof['original_ancestor_zero_mask_retained'] and proof['leaf_mask_fields_preserved']
    assert len(proof['samples']) == 12 and proof['gpu_final_cpu_double_error'] < 3e-5
    assert proof['gpu_curve_captured_raw_error'] < 3e-5 and proof['reopened_gpu_prefix_error'] < 1e-6
    assert proof['gpu_raw_sampling_cpu_double_error'] < 3e-5 and proof['gpu_packet_independent_curve_error'] < 3e-5
    assert proof['test_only_synthetic_negative_samples'] > 0 and proof['test_only_synthetic_above_one_samples'] > 0
    assert proof['test_only_synthetic_prefix_error'] < 3e-5 and proof['test_only_synthetic_composite_error'] < 3e-5
    assert proof['original_leaf_restored_before_save']
    assert proof['source_free_cold_packed_open'] and proof['effects_descriptor_sha_preserved']
    assert proof['three_bindings_one_pixel_owner'] and proof['reopened_gpu_error'] < 1e-6
    # Content+mask+existing Worn preload must share a single GPU source texture.
    # Open recreates the same storage identity; if the old live GPU cache keeps
    # it, reuse is permitted. Computed 128px mask prefixes are separate outputs.
    first, reopened = log.split('BTS_BITMAP_MASK_RESOURCE_PHASE first', 1)[1].split('BTS_BITMAP_MASK_RESOURCE_PHASE reopened', 1)
    allocation_lines = lambda phase: [line for line in phase.splitlines() if 'BTS_GPU_RESOURCE_ALLOC key=resource:' + proof['source_storage_identity'] in line]
    allocations, reopened_allocations = allocation_lines(first), allocation_lines(reopened)
    assert len(allocations) == 1 and len(reopened_allocations) <= 1, (allocations, reopened_allocations)
    assert not proof['appearance_verified']
    proof['source_gpu_allocations_for_three_bindings'] = len(allocations)
    proof['reopened_source_gpu_allocations_for_three_bindings'] = len(reopened_allocations)
    (output / 'verified.json').write_text(json.dumps(proof, indent=2) + '\n')
    print('Actual isolated bitmap mask GPU/raw-half oracle, one GPU source and cold packed Open PASS')
