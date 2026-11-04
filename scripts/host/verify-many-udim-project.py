"""Build a separate native regression from existing host objects; never rebuild the host.

Run inside the normal build environment, after coordinating a build slot:
  nice -n19 python3 scripts/host/verify-many-udim-project.py --output "$BTS_ROOT/build/many-udim"
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
p.add_argument('--production-source-root', type=Path, help='Optional stable worktree for newer production units')
p.add_argument('--output', type=Path, required=True)
p.add_argument('--compile-unit', action='append', default=[])
p.add_argument('--object', action='append', default=[])
p.add_argument('--build-only', action='store_true')
p.add_argument('--run-only', action='store_true')
a = p.parse_args()
repo = Path(__file__).resolve().parents[2]
production = (a.production_source_root or repo).resolve()
bts = Path(os.environ.get('BTS_ROOT', repo.parent)).resolve()
build = (a.host_build or bts / 'build/host').resolve()
output = a.output.resolve()
if not output.is_relative_to(bts / 'build') or output == build or output.is_relative_to(build):
    raise SystemExit('The private output must be under BTS_ROOT/build, outside the host build')
output.mkdir(parents=True, exist_ok=True)
(output / 'bin').mkdir(exist_ok=True)
binary = output / 'bin/many-udim-project-native-test'
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
    tasks = [(name, production / 'native/host' / name) for name in a.compile_unit]
    tasks.append(('many_udim_project_native_test.cc', repo / 'native/tests/many_udim_project_native_test.cc'))
    def compile_one(task):
        name, source = task
        args = compilation('MeshBaker.cc' if name == 'many_udim_project_native_test.cc' else name)
        previous = args[args.index('-o') + 1]
        old_source_root = Path(args[args.index('-c') + 1]).parent
        source_root = repo if name == 'many_udim_project_native_test.cc' else production
        args = [arg.replace(str(old_source_root), str(source_root / 'native/host')) for arg in args]
        obj = output / (name + '.o')
        args[args.index('-o') + 1] = str(obj)
        args[args.index('-MF') + 1] = str(obj) + '.d'
        args[args.index('-c') + 1] = str(source)
        print(f'Compile {name}', flush=True)
        with (output / (name + '.compile.log')).open('w') as log:
            subprocess.run(['nice', '-n', '19', *args], cwd=build, env=env, stdout=log, stderr=subprocess.STDOUT, check=True)
        return previous if name != 'many_udim_project_native_test.cc' else '__test_main__', str(obj)
    with ThreadPoolExecutor(max_workers=2) as pool:
        replacements.update(pool.map(compile_one, tasks))
    link = shlex.split(commands[-1].split(' && ')[1])
    link = [arg for arg in link if not arg.endswith('/main.cc.o') and arg not in replacements]
    first_object = next(i for i, arg in enumerate(link) if arg.endswith('.o'))
    link[first_object:first_object] = replacements.values()
    link[link.index('-o') + 1] = str(binary)
    print('Link private native regression', flush=True)
    with (output / 'link.log').open('w') as log:
        subprocess.run(['nice', '-n', '19', *link], cwd=build, env=env, stdout=log, stderr=subprocess.STDOUT, check=True)
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
    with (output / 'run.log').open('w') as log:
        subprocess.run(['nice', '-n', '19', 'bash', str(repo / 'scripts/host/run-host.sh'), '--headless-many-udim', str(output)], env=env, cwd=repo, stdout=log, stderr=subprocess.STDOUT, check=True, timeout=240)
    proof = json.loads((output / 'many-udim-project.json').read_text())
    assert len(proof['tiles']) == 18 and proof['tiles'][-1] == 2000 and proof['visible_windows'] == 0
    log = (output / 'run.log').read_text()
    assert 'BTS_SHUTDOWN clean' in log and 'Not freed memory blocks' not in log
    assert proof['exact_authored_hashes'] and not proof['shader_failures']
    print('Native eighteen-tile New/UV/viewport/Save/Open and tile2000 GPU context PASS')
