# SPDX-License-Identifier: GPL-3.0-or-later
"""Build a separate native regression from existing host objects; never rebuild the host.

Run only after the host build is stable, after coordinating a build/GPU slot:
  nice -n19 python3 scripts/host/verify-async-project-open.py --output "$BTS_ROOT/build/async-project-open" --fixture /path/to/private/used_varnish_wood.spsm
Production units that changed after the host build must be named with --compile-unit
or supplied as NAME.cc=OBJECT via --object. All new mutable outputs are private.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--host-build', type=Path)
p.add_argument('--fixture', type=Path, required=True, help='Wood SPSM source copied into this private output before testing')
p.add_argument('--build-workspace', action='store_true', help='Also compile/link the actual workspace main with the changed production objects')
p.add_argument('--production-source-root', type=Path, help='Optional stable worktree for newer production units')
p.add_argument('--output', type=Path, required=True)
p.add_argument('--compile-unit', action='append', default=[])
p.add_argument('--object', action='append', default=[])
p.add_argument('--build-only', action='store_true')
p.add_argument('--compile-only', action='store_true', help='Compile private objects without reading/linking mutable host artifacts')
p.add_argument('--link-only', action='store_true', help='Link previously compiled private objects after the host build is stable')
p.add_argument('--run-only', action='store_true')
a = p.parse_args()
repo = Path(__file__).resolve().parents[2]
production = (a.production_source_root or repo).resolve()
bts = Path(os.environ.get('BTS_ROOT', repo.parent)).resolve()
import sys
sys.path.insert(0, str(repo / 'scripts/host'))
from main_source import host_file  # host files live in domain folders
build = (a.host_build or bts / 'build/host').resolve()
output = a.output.resolve()
if not output.is_relative_to(bts / 'build') or output == build or output.is_relative_to(build):
    raise SystemExit('The private output must be under BTS_ROOT/build, outside the host build')
output.mkdir(parents=True, exist_ok=True)
(output / 'bin').mkdir(exist_ok=True)
binary = output / 'bin/async-project-open-native-test'
env = dict(os.environ, CCACHE_DIR=str(bts / 'build/ccache'))
if not a.run_only:
    ninja = shutil.which('ninja-build') or shutil.which('ninja')
    commands = subprocess.check_output([ninja, '-t', 'commands', 'bts-host'], cwd=build, text=True).splitlines()
    def compilation(name):
        return shlex.split(next(c for c in commands if ' -o ' in c and '/' + name + '.o -c ' in c))
    def object_for(name):
        args = compilation(name)
        return args[args.index('-o') + 1]
    replacements = {}
    for value in a.object:
        name, path = value.split('=', 1)
        path = Path(path).resolve()
        if not path.is_file():
            raise SystemExit(f'Missing replacement object: {path}')
        replacements[object_for(name)] = str(path)
    tasks = [(name, host_file(production, name)) for name in a.compile_unit]
    if a.build_workspace: tasks.append(('main.cc', production / 'native/host/main.cc'))
    tasks.append(('async_project_open_native_test.cc', repo / 'native/tests/async_project_open_native_test.cc'))
    def compile_one(task):
        name, source = task
        extra = name == 'SpsmBitmapLowering.cc' and not any('/SpsmBitmapLowering.cc.o -c ' in c for c in commands)
        args = compilation('MeshBaker.cc' if name == 'async_project_open_native_test.cc' else 'SpsmSource.cc' if extra else name)
        previous = args[args.index('-o') + 1]
        old_source = args[args.index('-c') + 1]
        old_source_root = old_source[:old_source.index('/native/host') + len('/native/host')]
        source_root = repo if name == 'async_project_open_native_test.cc' else production
        args = [arg.replace(str(old_source_root), str(source_root / 'native/host')) for arg in args]
        obj = output / (name + '.o')
        args[args.index('-o') + 1] = str(obj)
        args[args.index('-MF') + 1] = str(obj) + '.d'
        args[args.index('-c') + 1] = str(source)
        if name == 'async_project_open_native_test.cc': args.append('-UNDEBUG')
        args = ['-O1' if arg in ('-O2', '-O3') else arg for arg in args]
        print(f'Compile {name}', flush=True)
        with (output / (name + '.compile.log')).open('w') as log:
            subprocess.run(['nice', '-n', '19', *args], cwd=build, env=env, stdout=log, stderr=subprocess.STDOUT, check=True)
        return '__test_main__' if name == 'async_project_open_native_test.cc' else '__extra__' + name if extra else previous, str(obj)
    if a.link_only:
        replacements.update(json.loads((output / "compiled.json").read_text()))
    else:
        with ThreadPoolExecutor(max_workers=2) as pool:
            replacements.update(pool.map(compile_one, tasks))
        (output / "compiled.json").write_text(json.dumps(replacements, indent=2))
    if a.compile_only:
        print("Private compilation PASS; no host artifacts linked")
        raise SystemExit(0)
    link = shlex.split(commands[-1].split(' && ')[1])
    main_object = object_for('main.cc')
    workspace_main = replacements.pop(main_object, None)
    link = [arg for arg in link if not arg.endswith('/main.cc.o') and arg not in replacements]
    first_object = next(i for i, arg in enumerate(link) if arg.endswith('.o'))
    link[first_object:first_object] = replacements.values()
    link[link.index('-o') + 1] = str(binary)
    print('Link private native regression', flush=True)
    with (output / 'link.log').open('w') as log:
        subprocess.run(['nice', '-n', '19', *link], cwd=build, env=env, stdout=log, stderr=subprocess.STDOUT, check=True)
    if workspace_main:
        workspace_link = [workspace_main if arg == replacements['__test_main__'] else arg for arg in link]
        workspace_link[workspace_link.index('-o') + 1] = str(output / 'bin/bts-host')
        with (output / 'workspace-link.log').open('w') as log:
            subprocess.run(['nice', '-n', '19', *workspace_link], cwd=build, env=env, stdout=log, stderr=subprocess.STDOUT, check=True)
    for resource in (build / 'bin').iterdir():
        if resource.is_dir() and resource.name[0].isdigit():
            local = binary.parent / resource.name
            if not local.exists():
                local.symlink_to(resource, target_is_directory=True)
    (output / 'build.json').write_text(json.dumps({'source': str(repo), 'production_source': str(production), 'host_build': str(build), 'compiled_units': a.compile_unit, 'replacements': replacements}, indent=2) + '\n')
if not a.build_only:
    env.update(BTS_ROOT=str(bts), BTS_HOST_EXECUTABLE=str(binary), QT_QPA_PLATFORM='offscreen',
               BTS_ENVIRONMENT=env.get('BTS_ENVIRONMENT', str(bts / 'build/environment-cache/TT_environmentLights_v001.hdr')),
               XDG_CONFIG_HOME=str(output / 'config'), XDG_DATA_HOME=str(output / 'data'),
               XDG_CACHE_HOME=str(output / 'cache'), BTS_RECOVERY_DIR=str(output / 'recovery'),
               BTS_SESSION_LOG_DIR=str(output / 'logs'), BTS_RESOURCE_CACHE=str(output / 'resources'))
    inputs = output / 'inputs'
    inputs.mkdir(exist_ok=True)
    fixture = inputs / 'used_varnish_wood.spsm'
    held = Path(str(fixture) + '.held')
    if held.exists(): held.unlink()
    # Both cache roots belong to this verifier, never an artist's cache.
    shutil.rmtree(output / 'first-cache', ignore_errors=True)
    shutil.rmtree(output / 'second-cache', ignore_errors=True)
    shutil.rmtree(output / 'third-cache', ignore_errors=True)
    shutil.copy2(a.fixture.resolve(), fixture)
    with (output / 'run.log').open('w') as log:
        subprocess.run(['nice', '-n', '19', 'bash', str(repo / 'scripts/host/run-host.sh'), str(output), str(fixture)], env=env, cwd=repo, stdout=log, stderr=subprocess.STDOUT, check=True, timeout=240)
    proof = json.loads((output / 'verified.json').read_text())
    log = (output / 'run.log').read_text()
    assert 'BTS_SHUTDOWN clean' in log and 'Not freed memory blocks' not in log
    assert proof['baseline_unresolved_bitmap'] and proof['open_heartbeats'] > 1
    assert proof['cancel_preserved_old_main'] and proof['failure_preserved_old_main'] and proof['stale_revision_rejected']
    assert proof['new_cache_root'] and proof['cache_uuid_rebound'] and proof['original_input_missing'] and proof['gpu_max_error'] < 1e-6
    assert proof['batch_worker_open'] and proof['batch_gpu_max_error'] < 1e-6
    assert proof['cpu_composite'] == 0
    print('Async native packed bitmap Open: progress, cancel/failure/stale protection, cross-cache restore and GPU identity PASS')
