# SPDX-License-Identifier: GPL-3.0-or-later
"""Exercise ordinary Color request/install guards using real worker image decoding.

Only a driver and LayerApi are linked against immutable, ABI-matching host
inputs. main.cc is compiled separately to verify both production guard sites.
This is a controller/guard proof, not Workspace automation or a GPU proof.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shlex
import subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--host-snapshot', type=Path, required=True)
p.add_argument('--output', type=Path, required=True)
p.add_argument('--host-resources', type=Path, required=True,
               help='Stable versioned Blender runtime-data directory parent; no build objects are taken from it')
p.add_argument('--compile-only', action='store_true')
p.add_argument('--run-only', action='store_true')
a = p.parse_args()
repo = Path(os.environ.get('BTS_REPO', Path(__file__).resolve().parents[2])).resolve()
snap, out = a.host_snapshot.resolve(), a.output.resolve()
if not out.is_relative_to(repo.parent / 'build') or out.is_relative_to(snap) or out.is_relative_to(repo.parent / 'build/host'):
    raise SystemExit('Use a separate private build directory')
out.mkdir(parents=True, exist_ok=True)
commands = json.loads((snap / 'host-commands.json').read_text())
snapshot_metadata = json.loads((snap / 'snapshot.json').read_text())
template = shlex.split(next(c for c in commands if '/main.cc.o -c ' in c))
old = Path(template[template.index('-c') + 1]).parents[2]
env = dict(os.environ, CCACHE_DIR=str(repo.parent / 'build/ccache'))
replacements = {}
source_names = ['native/host/LayerApi.cc', 'native/host/main.cc', 'native/host/io/OrdinaryColorSource.hh', 'native/tests/ordinary_color_source_test.cc', 'native/tests/OrderedRawRgbFixture.hh']
source_hashes = {name: hashlib.sha256((repo / name).read_bytes()).hexdigest() for name in source_names}
if not a.run_only:
    for source in ['native/host/LayerApi.cc', 'native/host/main.cc', 'native/tests/ordinary_color_source_test.cc']:
        command = template.copy()
        if Path(source).name == 'LayerApi.cc':
            command = shlex.split(next(c for c in commands if '/LayerApi.cc.o -c ' in c))
        original = command[command.index('-o') + 1]
        obj = out / (Path(source).name + '.o')
        command = [x.replace(str(old), str(repo)) for x in command]
        command[command.index('-c') + 1] = str(repo / source)
        command[command.index('-o') + 1] = str(obj)
        for option, value in [('-MF', str(obj) + '.d'), ('-MT', str(obj))]:
            if option in command:
                command[command.index(option) + 1] = value
        if Path(source).parent.name == 'tests':
            command.append('-UNDEBUG')
        print('Compile', source, flush=True)
        with (out / (Path(source).name + '.compile.log')).open('w') as log:
            subprocess.run(['nice', '-n', '19', *command], cwd=snap, env=env, stdout=log, stderr=subprocess.STDOUT, check=True)
        if Path(source).name != 'main.cc':
            replacements[original] = str(obj)
    main = template[template.index('-o') + 1]
    replacements[main] = str(out / 'ordinary_color_source_test.cc.o')
    parts = commands[-1].split(' && ')
    link = shlex.split(next(part for part in parts if ' -o ' in part))
    link = [replacements.get(x, x) for x in link]
    executable = out / 'ordinary-color-source-test'
    link[link.index('-o') + 1] = str(executable)
    link = [('-Wl,--dependency-file=' + str(out / 'link.d')) if x.startswith('-Wl,--dependency-file=')
            else x.replace(snapshot_metadata['source_build'] + '/bin/lib', str(snap / 'bin/lib')) for x in link]
    inputs = []
    for token in link:
        if not token.endswith(('.o', '.a')):
            continue
        path = Path(token)
        if not path.is_absolute():
            path = snap / path
        path = path.resolve()
        # Immutable SDK archives outside the artist build are permitted. Every
        # build-generated .o/.a must come from the frozen snapshot/private output.
        if not path.is_file() or path.is_relative_to(Path(snapshot_metadata['source_build']).resolve()):
            raise SystemExit('Missing/mutable artist link input: ' + str(path))
        inputs.append({'path': str(path), 'sha256': hashlib.sha256(path.read_bytes()).hexdigest()})
    (out / 'link-inputs.json').write_text(json.dumps(inputs, indent=2) + '\n')
    with (out / 'link.log').open('w') as log:
        subprocess.run(['nice', '-n', '19', *link], cwd=snap, env=env, stdout=log, stderr=subprocess.STDOUT, check=True)
    for resource in a.host_resources.resolve().iterdir():
        if resource.is_dir() and resource.name[0].isdigit() and not (out / resource.name).exists():
            (out / resource.name).symlink_to(resource, target_is_directory=True)
    (out / 'source-hashes.json').write_text(json.dumps({'snapshot_head': snapshot_metadata['source_head'], 'sources': source_hashes}, indent=2) + '\n')
if a.compile_only:
    raise SystemExit(0)
proof = json.loads((out / 'source-hashes.json').read_text())
if proof['snapshot_head'] != snapshot_metadata['source_head'] or proof['sources'] != source_hashes:
    raise SystemExit('Compiled source hashes differ; rebuild before verification')
env.update(QT_QPA_PLATFORM='offscreen', BTS_HEADLESS_WORKSPACE='1', LD_LIBRARY_PATH=str(snap / 'bin/lib') + ':' + env.get('LD_LIBRARY_PATH', ''))
(out / 'tmp').mkdir(exist_ok=True)
env['TMPDIR'] = str(out / 'tmp')
env['QT_PLUGIN_PATH'] = str(repo.parent / 'qt/6.8.3/plugins')
for key, name in [('XDG_CONFIG_HOME', 'config'), ('XDG_DATA_HOME', 'data'), ('XDG_STATE_HOME', 'state'), ('XDG_CACHE_HOME', 'cache'), ('BTS_RECOVERY_DIR', 'recovery'), ('BTS_RESOURCE_CACHE', 'resources'), ('BTS_SESSION_LOG_DIR', 'logs')]:
    env[key] = str(out / name)
with (out / 'run.log').open('w') as log:
    subprocess.run(['nice', '-n', '19', str(out / 'ordinary-color-source-test'), str(out)], cwd=repo, env=env, stdout=log, stderr=subprocess.STDOUT, check=True, timeout=100)
text = (out / 'run.log').read_text()
if 'Ordinary Color source guard PASS' not in text or 'Not freed memory blocks' in text:
    raise SystemExit(text[-5000:])
(out / 'verified.json').write_text(json.dumps({'real_decodes': ['canonical.png', 'canonical.exr', 'canonical.btsmap'], 'request_reject_before_work': True, 'queued_owner_install_recheck': True, 'preparation_release': True, 'unchanged_document_source_history': True, 'disabled_boundary_still_guards': True, 'tint_mapping_other_channel_ag_undo': True, 'main_request_install_compiled': True, 'gpu_contexts': 0, 'scope': 'real TextureLoadController + shared production guard; no full Workspace automation', 'executable_sha256': hashlib.sha256((out / 'ordinary-color-source-test').read_bytes()).hexdigest()}, indent=2) + '\n')
print(text)
