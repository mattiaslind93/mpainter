"""Real Wood raw RGB/source Levels/named OCIO -> native GPU and cold packed Open.

Requires the verified target-qualified CPU overlay from verify-spsm-color-bitmaps.
Coordinate a correctness GPU slot before running; no artist build inputs are used.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--host-snapshot', type=Path, required=True)
p.add_argument('--cpu-overlay', type=Path, required=True)
p.add_argument('--host-resources', type=Path, required=True)
p.add_argument('--output', type=Path, required=True)
p.add_argument('--fixture', type=Path, required=True)
p.add_argument('--build-only', action='store_true')
p.add_argument('--run-only', action='store_true')
p.add_argument('--cold-only', action='store_true', help='Resume only the checkpoint-bound fresh-process packed Open')
a = p.parse_args()
repo = Path(os.environ.get('BTS_REPO', Path(__file__).resolve().parents[2])).resolve()
snap, overlay, out = a.host_snapshot.resolve(), a.cpu_overlay.resolve(), a.output.resolve()
if not out.is_relative_to(repo.parent / 'build') or out.is_relative_to(snap) or out.is_relative_to(repo.parent / 'build/host'):
    raise SystemExit('Use separate private build storage')
out.mkdir(parents=True, exist_ok=True)
metadata = json.loads((snap / 'snapshot.json').read_text())
proof = json.loads((overlay / 'production-source-hashes.json').read_text())
if proof['snapshot_head'] != metadata['source_head'] or any(hashlib.sha256((repo / name).read_bytes()).hexdigest() != digest for name, digest in proof['sources'].items()):
    raise SystemExit('CPU overlay production hashes differ; rebuild the include closure first')
source = repo / 'native/tests/spsm_bitmap_color_native_test.cc'
source_hashes = {str(path.relative_to(repo)): hashlib.sha256(path.read_bytes()).hexdigest() for path in [source, repo / 'native/tests/spsm_bitmap_levels_native_test.cc']}
source_text = source.read_text()
# Exclude only cold-process routing from the unchanged numerical stage oracle.
oracle_parts = [source_text.split('int main(int argc', 1)[0],
                source_text.split('NewProjectOptions options;', 1)[1].split('const auto publish=', 1)[0],
                source_text.split('const std::vector<std::array<int,2>> probes', 1)[1].split('// Completed stage oracle checkpoint:', 1)[0].replace('  if(!cold_only){\n   StudioAsset', '  {\n   StudioAsset').replace('  if(!cold_only){\n  ', '  '),
                (repo / 'native/tests/spsm_bitmap_levels_native_test.cc').read_text()]
oracle_hash = hashlib.sha256('\n'.join(oracle_parts).encode()).hexdigest()
binding = hashlib.sha256(json.dumps({'snapshot_head': metadata['source_head'], 'production_sources': proof['sources'], 'oracle_sha256': oracle_hash}, sort_keys=True).encode()).hexdigest()

commands = json.loads((snap / 'host-commands.json').read_text())
template = shlex.split(next(c for c in commands if '/main.cc.o -c ' in c))
old = Path(template[template.index('-c') + 1]).parents[2]
env = dict(os.environ, CCACHE_DIR=str(repo.parent / 'build/ccache'))
binary = out / 'spsm-color-native-test'
if not a.run_only:
    if a.cold_only and (out / 'build.json').exists() and not (out / 'stage-build.json').exists():
        shutil.copyfile(out / 'build.json', out / 'stage-build.json')
    args = [x.replace(str(old), str(repo)) for x in template]
    obj = out / 'driver.o'
    args[args.index('-c') + 1], args[args.index('-o') + 1] = str(source), str(obj)
    for key, value in [('-MF', str(obj) + '.d'), ('-MT', str(obj))]:
        if key in args:
            args[args.index(key) + 1] = value
    args.append('-UNDEBUG')
    with (out / 'compile.log').open('w') as log:
        subprocess.run(['nice', '-n', '19', *args], cwd=snap, env=env, stdout=log, stderr=subprocess.STDOUT, check=True)
    replacements = json.loads((overlay / 'objects.json').read_text())
    main = template[template.index('-o') + 1]
    replacements[main] = str(obj)
    link = [replacements.get(x, x) for x in shlex.split(next(part for part in commands[-1].split(' && ') if ' -o ' in part))]
    extras = [value for key, value in replacements.items() if '/native/tests/' in key and not key.startswith('__tests__/')]
    # Newly embedded CPU regressions satisfy the production dispatcher reference.
    first = next(i for i, token in enumerate(link) if token.endswith('.o'))
    link[first:first] = [x for x in extras if x not in link]
    link[link.index('-o') + 1] = str(binary)
    link = [('-Wl,--dependency-file=' + str(out / 'link.d')) if x.startswith('-Wl,--dependency-file=') else x.replace(metadata['source_build'] + '/bin/lib', str(snap / 'bin/lib')) for x in link]
    inputs = []
    for token in link:
        if not token.endswith(('.o', '.a')):
            continue
        path = Path(token)
        if not path.is_absolute():
            path = snap / path
        path = path.resolve()
        if not path.is_file() or path.is_relative_to(Path(metadata['source_build']).resolve()):
            raise SystemExit('Mutable/missing link input: ' + str(path))
        inputs.append(str(path))
    with (out / 'link.log').open('w') as log:
        subprocess.run(['nice', '-n', '19', *link], cwd=snap, env=env, stdout=log, stderr=subprocess.STDOUT, check=True)
    for resource in a.host_resources.resolve().iterdir():
        if resource.is_dir() and resource.name[0].isdigit() and not (out / resource.name).exists():
            (out / resource.name).symlink_to(resource, target_is_directory=True)
    (out / 'build.json').write_text(json.dumps({'snapshot_head': metadata['source_head'], 'production_sources': proof['sources'], 'test_sources': source_hashes, 'oracle_sha256': oracle_hash, 'checkpoint_binding': binding, 'link_inputs': inputs, 'binary_sha256': hashlib.sha256(binary.read_bytes()).hexdigest()}, indent=2) + '\n')
if a.build_only:
    raise SystemExit(0)
build = json.loads((out / 'build.json').read_text())
if build['production_sources'] != proof['sources'] or build['test_sources'] != source_hashes or build['binary_sha256'] != hashlib.sha256(binary.read_bytes()).hexdigest():
    raise SystemExit('GPU proof build no longer matches sources')
for name in ['config', 'data', 'state', 'cache', 'recovery', 'logs', 'resources', 'tmp', 'inputs']:
    (out / name).mkdir(exist_ok=True)
fixture = out / 'inputs/used_varnish_wood.spsm'
if not a.cold_only:
    Path(str(fixture) + '.held').unlink(missing_ok=True)
    subprocess.run(['cp', '--reflink=auto', str(a.fixture.resolve()), str(fixture)], check=True)
    for name in ['first-cache', 'second-cache']:
        shutil.rmtree(out / name, ignore_errors=True)
env.update(BTS_COLOR_PROOF_BINDING=binding, BTS_ROOT=str(repo.parent), BTS_HOST_EXECUTABLE=str(binary), QT_QPA_PLATFORM='offscreen', BTS_HEADLESS_WORKSPACE='1', LD_LIBRARY_PATH=str(snap / 'bin/lib') + ':' + env.get('LD_LIBRARY_PATH', ''), QT_PLUGIN_PATH=str(repo.parent / 'qt/6.8.3/plugins'), TMPDIR=str(out / 'tmp'))
for key, name in [('XDG_CONFIG_HOME', 'config'), ('XDG_DATA_HOME', 'data'), ('XDG_STATE_HOME', 'state'), ('XDG_CACHE_HOME', 'cache'), ('BTS_RECOVERY_DIR', 'recovery'), ('BTS_RESOURCE_CACHE', 'resources'), ('BTS_SESSION_LOG_DIR', 'logs')]:
    env[key] = str(out / name)
def run(cold_only):
    filename = 'restart-run.log' if cold_only else 'run.log'
    args = ['nice', '-n', '19', 'bash', str(repo / 'scripts/host/run-host.sh'), str(out), str(fixture)]
    if cold_only:
        shutil.rmtree(out / 'restart-cache', ignore_errors=True)
        args.append('--cold-only')
    with (out / filename).open('w') as log:
        result = subprocess.run(args, cwd=repo, env=env, stdout=log, stderr=subprocess.STDOUT, timeout=600)
    log = (out / filename).read_text()
    if result.returncode or (not cold_only and 'background=1 cpu_composite=0' not in log) or 'BTS_SPSM_COLOR_GPU PASS' not in log or 'BTS_SHUTDOWN clean' not in log or 'Not freed memory blocks' in log:
        raise SystemExit('Native Color proof failed: ' + log[-6000:])
    verified = json.loads((out / ('verified-restart.json' if cold_only else 'verified.json')).read_text())
    assert len(verified['cases']) == 4 and verified['components'] == 288 and verified['levels_named_ocio_on_f32_packet_maximum_error'] < 3e-5 and verified['raw_f32_affine_bilinear_maximum_error'] < 3e-5
    assert verified['source_hidden_new_cache_packed_open'] and verified['cold_open_maximum_error'] < 1e-6 and not verified['source_appearance_verified']
    assert verified['fresh_process_restart'] == cold_only
if not a.cold_only:
    run(False)
run(True)
print('Actual Wood explicit native RGB Levels GPU + fresh-process source-free cold packed Open PASS')
