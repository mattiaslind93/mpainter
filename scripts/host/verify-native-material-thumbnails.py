# SPDX-License-Identifier: GPL-3.0-or-later
"""Exercise authored/native Shelf previews and a fresh, source-free packed Open."""
import argparse
import json
import os
import shutil
import subprocess
from pathlib import Path

p = argparse.ArgumentParser()
p.add_argument('--output', type=Path, required=True)
p.add_argument('--host', type=Path, required=True)
p.add_argument('--blender', type=Path)
p.add_argument('--height-actor', type=Path, help='Optional existing synthetic Height alias actor')
a = p.parse_args()
repo = Path(__file__).resolve().parents[2]
bts = Path(os.environ.get('BTS_ROOT', repo.parent)).resolve()
out = a.output.resolve()
assert out.is_relative_to(bts / 'build')
out.mkdir(parents=True, exist_ok=True)
source = out / 'source' / 'known-previews.blend'
env = dict(os.environ, QT_QPA_PLATFORM='offscreen', BTS_HEADLESS_WORKSPACE='1',
           BTS_NATIVE_MATERIAL_HOST_QUEUE_TEST='1', BTS_NATIVE_THUMBNAIL_TEST='1',
           BTS_NATIVE_THUMBNAIL_SOURCE=str(source), BTS_HOST_EXECUTABLE=str(a.host.resolve()),
           BTS_NATIVE_THUMBNAIL_OUTPUT=str(out), BTS_GL_DEBUG='1')
# Match run-host.sh's approved environment selection for standalone actors too.
if not env.get('BTS_ENVIRONMENT'):
    artist_config = Path(env.get('BTS_USER_CONFIG_DIR', bts.parent / '.config'))
    for version in ('5.2', '5.1', '5.0'):
        candidate = artist_config / 'blender' / version / 'datafiles/studiolights/world/TT_environmentLights_v001.hdr'
        if candidate.is_file():
            env['BTS_ENVIRONMENT'] = str(candidate)
            break
for key, folder in [('XDG_CONFIG_HOME', 'config'), ('XDG_DATA_HOME', 'data'),
                    ('XDG_CACHE_HOME', 'cache'), ('TMPDIR', 'tmp'),
                    ('BTS_RECOVERY_DIR', 'recovery'), ('BTS_RESOURCE_CACHE', 'resources'),
                    ('BTS_SESSION_LOG_DIR', 'logs')]:
    (out / folder).mkdir(exist_ok=True)
    env[key] = str(out / folder)
blender = a.blender or bts / 'build/baseline/bin/blender'
with (out / 'fixture.log').open('w') as log:
    subprocess.run(['nice', '-n', '19', str(blender), '--background', '--factory-startup',
                    '--python', str(repo / 'scripts/host/generate-native-thumbnail-fixture.py'),
                    '--', str(source)], env=env, stdout=log, stderr=subprocess.STDOUT,
                   check=True, timeout=90)
results = {}
for stage in ('warm', 'cold'):
    if stage == 'cold':
        shutil.rmtree(source.parent)
        shutil.rmtree(out / 'resources')
        (out / 'resources').mkdir()
        shutil.rmtree(out / 'cache')
        (out / 'cache').mkdir()
        env['BTS_NATIVE_THUMBNAIL_COLD'] = '1'
    with (out / f'{stage}.log').open('w') as log:
        subprocess.run(['nice', '-n', '19', 'bash', str(repo / 'scripts/host/run-host.sh'),
                        '--no-splash', '--headless-viewport-test', str(out)], env=env,
                       stdout=log, stderr=subprocess.STDOUT, check=True, timeout=240)
    log = (out / f'{stage}.log').read_text()
    assert 'BTS_HOST_FAILED' not in log and 'BTS_SHUTDOWN clean' in log, log[-8000:]
    assert 'BTS_GL_ERROR' not in log and 'Not freed memory blocks' not in log
    proof = json.loads((out / 'native-host-queue.json').read_text())['native_thumbnails']
    results[stage] = proof
    (out / f'{stage}-native-host-queue.json').write_text(json.dumps({'native_thumbnails': proof}, indent=2) + '\n')
assert results['warm']['saved_preview_exact'] and results['warm']['no_preview_evaluated_channels']
assert results['warm']['legacy_shelf_generation'] and results['warm']['legacy_preview_error_resolved']
assert results['warm']['published_asset_reensure_no_gpu_work']
assert results['cold']['fresh_process_project_open'] and results['cold']['source_absent']
assert results['cold']['saved_preview_exact'] and results['cold']['generated_preview_exact']
(out / 'verified-thumbnails.json').write_text(json.dumps(results, indent=2) + '\n')
if a.height_actor:
    height_env = dict(env)
    for key in ('BTS_NATIVE_MATERIAL_HOST_QUEUE_TEST', 'BTS_NATIVE_THUMBNAIL_TEST',
                'BTS_NATIVE_THUMBNAIL_COLD', 'BTS_NATIVE_MATERIAL_GPU_TEST',
                'BTS_NATIVE_THUMBNAIL_SOURCE', 'BTS_NATIVE_THUMBNAIL_OUTPUT'):
        height_env.pop(key, None)
    height_env['BTS_ROOT'] = str(bts)
    height_env['QT_PLUGIN_PATH'] = str(bts / 'qt/6.8.3/plugins')
    height_env['LD_LIBRARY_PATH'] = (str(bts / 'qt/6.8.3/lib') + ':' +
                                    str(bts / 'build/host/bin/lib') + ':' +
                                    height_env.get('LD_LIBRARY_PATH', ''))
    with (out / 'height-alias.log').open('w') as log:
        subprocess.run(['nice', '-n', '19', 'bash', '-c',
                        'source "$1"; shift; exec "$@"', 'bg-height-alias',
                        str(repo / 'scripts/host/gpu-env.sh'), str(a.height_actor.resolve()),
                        str(out / 'height-alias')], env=height_env, stdout=log,
                       stderr=subprocess.STDOUT, check=True, timeout=240)
    results['height_alias'] = json.loads((out / 'height-alias/verified.json').read_text())
    assert results['height_alias']['preview_only_hash_alias_accepted']
    assert results['height_alias']['different_source_height_alias_rejected']
(out / 'verified-thumbnails.json').write_text(json.dumps(results, indent=2) + '\n')
print(json.dumps(results, indent=2))
