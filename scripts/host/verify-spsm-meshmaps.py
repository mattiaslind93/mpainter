# SPDX-License-Identifier: GPL-3.0-or-later
"""Compile/run SPSM mesh-map CPU/worker/native GPU proofs against frozen host libraries.

Only private objects/executables are written. GPU contexts run only with --suite gpu.
Run nice -n19 inside the build container; no host build is started.
"""
import argparse
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--suite', choices=['cpu','source','gpu'], default='cpu')
stages=parser.add_mutually_exclusive_group()
stages.add_argument('--compile-only', action='store_true')
stages.add_argument('--run-only', action='store_true')
parser.add_argument('--build-dir', type=Path, required=True)
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--fixtures', type=Path)
parser.add_argument('--sanitize', action='store_true')
parser.add_argument('--runtime-baseline', action='store_true')
parser.add_argument('--lsan-suppressions', type=Path)
parser.add_argument('--host-snapshot', type=Path, required=True,
                    help='Immutable private snapshot of host .o/.a link inputs, created before any host rebuild')
parser.add_argument('--graph-dir', type=Path,
                    help='Stable Ninja graph matching the frozen host objects (defaults to build-dir)')
args = parser.parse_args()
build, output = args.build_dir.resolve(), args.output.resolve()
output.mkdir(parents=True, exist_ok=True)
root = Path(__file__).resolve().parents[2]
if not output.is_relative_to(root.parent/'build') or output.is_relative_to(build):
    raise RuntimeError('Output must be private under the workspace build directory, outside the artist build')
if args.host_snapshot.resolve().is_relative_to(build):
    raise RuntimeError('Host snapshot must be private, outside the artist build')
ninja = shutil.which('ninja') or shutil.which('ninja-build')
graph = args.graph_dir.resolve() if args.graph_dir else build
frozen_commands=args.host_snapshot.resolve() / 'frozen-commands.json'
commands=json.loads(frozen_commands.read_text()) if frozen_commands.is_file() else subprocess.check_output([ninja, '-t', 'commands', 'bts-host'], cwd=graph, text=True).splitlines()
base = shlex.split(next(c for c in commands if '/main.cc.o' in c and ' -c ' in c))
main_object = base[base.index('-o') + 1]
sources = ['native/spsm/SpsmReader.cc', 'native/spsm/semantic/SpsmSemantic.cc',
           'native/host/SpsmMaterialImport.cc', 'native/host/SpsmMeshMapLowering.cc']
if args.suite=='source':
    sources += ['native/host/'+name for name in ['SpsmSource.cc','SpsmBitmapLowering.cc','SpsmBitmapTexture.cc','AssetCatalog.cc','SpsmAssetDispatch.cc']]
    sources += ['native/spsm/bitmaps/SpsmBitmap.cc']
sources += ['native/tests/'+{'cpu':'spsm_meshmap_lowering_test.cc','source':'spsm_meshmap_source_test.cc','gpu':'spsm_meshmap_native_test.cc'}[args.suite]]
hdf_flags = shlex.split(subprocess.check_output(['h5cc', '-show'], text=True))[1:]
hdf_cflags = [flag for flag in hdf_flags if flag.startswith(('-I', '-D'))]
hdf_libs = [flag for flag in hdf_flags if flag.startswith(('-L', '-l', '-Wl,'))]
objects, compile_commands = [], []
flags = ['-fsanitize=address,undefined', '-fno-omit-frame-pointer'] if args.sanitize else []
for source in sources:
    command = base.copy()
    obj = str(output / (Path(source).stem + '.o'))
    command[command.index('-o') + 1] = obj
    command[command.index('-c') + 1] = str(root / source)
    for option, value in (('-MF', obj + '.d'), ('-MT', obj)):
        if option in command:
            command[command.index(option) + 1] = value
    # Prefer headers from this worktree over the original host source graph.
    insert_at = 2 if Path(command[0]).name == 'ccache' else 1
    command[insert_at:insert_at] = ['-I' + str(root / 'native/host'), '-I' + str(root / 'native/spsm'),
                    '-I' + str(root / 'native/spsm/bitmaps'), '-I' + str(root / 'native/spsm/semantic')]
    command=['-O1' if v in ['-O2','-O3'] else v for v in command]
    command += hdf_cflags + ['-UNDEBUG'] + flags
    objects.append(obj)
    compile_commands.append(command)
link = shlex.split(commands[-1])
if link[:2] == [':', '&&']:
    link = link[2:]
if '&&' in link:
    link = link[:link.index('&&')]
if main_object not in link:
    raise RuntimeError('Cannot identify original main object')
link = [token for token in link if token != main_object and not any(
    token.endswith('/' + Path(source).name + '.o') for source in sources)]
snapshot = args.host_snapshot.resolve()
manifest=snapshot/'frozen-link-inputs.json'
frozen=json.loads(manifest.read_text()) if manifest.is_file() else {}
for i, token in enumerate(link):
    if not token.endswith(('.o', '.a')):
        continue
    if token in frozen:
        link[i]=frozen[token]
        continue
    path = (graph / token).resolve()
    if path.is_relative_to(graph):
        private = snapshot / path.relative_to(graph)
        if not private.is_file():
            # Private copies may have a different absolute host-source prefix
            # embedded in native object paths. Require a unique exact filename;
            # never fall back to any live object or guess between duplicates.
            matches = list(snapshot.rglob(path.name))
            if len(matches) != 1:
                raise RuntimeError('Missing/ambiguous frozen host link input: ' + str(private))
            private = matches[0]
        link[i] = str(private)
link[1:1] = objects
executable = output / ('spsm-meshmap-'+args.suite+'-test')
link[link.index('-o') + 1] = str(executable)
link = ['-Wl,--dependency-file=' + str(output / 'link.d')
        if token.startswith('-Wl,--dependency-file=') else token for token in link]
link += hdf_libs + ['-llz4'] + flags
(output / 'tmp').mkdir(exist_ok=True)
env = dict(os.environ, QT_QPA_PLATFORM='offscreen', TMPDIR=str(output / 'tmp'))
for key, name in [('XDG_CONFIG_HOME', 'config'), ('XDG_DATA_HOME', 'data'),
                  ('XDG_CACHE_HOME', 'cache'), ('BTS_RECOVERY_DIR', 'recovery'),
                  ('BTS_SESSION_LOG_DIR', 'logs'), ('BTS_RESOURCE_CACHE', 'resources')]:
    env[key] = str(output / name)
cache = (build / 'CMakeCache.txt').read_text()
for line in cache.splitlines():
    if line.startswith('Qt6Core_DIR:PATH='):
        sdk = Path(line.split('=', 1)[1]).parents[2]
        env['QT_PLUGIN_PATH'] = str(sdk / 'plugins')
        env['LD_LIBRARY_PATH'] = str(sdk / 'lib') + (':' + env['LD_LIBRARY_PATH'] if env.get('LD_LIBRARY_PATH') else '')
    if line.startswith('CMAKE_HOME_DIRECTORY:INTERNAL=') and not env.get('OCIO'):
        config = Path(line.split('=', 1)[1]) / 'release/datafiles/colormanagement/config.ocio'
        if config.is_file():
            env['OCIO'] = str(config)
    if line.startswith('PYTHON_EXECUTABLE:FILEPATH='):
        env['PYTHONHOME'] = str(Path(line.split('=', 1)[1]).parent.parent)
if args.sanitize:
    env.update(ASAN_OPTIONS='detect_leaks=1:halt_on_error=1', UBSAN_OPTIONS='halt_on_error=1')
if args.lsan_suppressions:
    env['LSAN_OPTIONS'] = 'suppressions=' + str(args.lsan_suppressions.resolve()) + ':print_suppressions=1'
run = [str(executable)] + (['--runtime-baseline'] if args.runtime_baseline else ([str(args.fixtures.resolve())] if args.fixtures else []))
if args.suite=='gpu':
    if args.sanitize or not args.fixtures:raise RuntimeError('GPU requires fixtures; use CPU/source suites for sanitizers')
    run=[str(executable),str(output/'gpu'),str(args.fixtures.resolve())]
    env.update(BTS_ROOT=str(root.parent),BTS_HEADLESS_WORKSPACE='1')
    for name in ['lib','5.2']:
        target=output/name
        if not target.exists():
            origin=snapshot/name if (snapshot/name).is_dir() else build/'bin'/name
            subprocess.run(['cp','-a','--reflink=auto',str(origin),str(target)],check=True)
with (output / 'run.log').open('w') as log:
    jobs=[] if args.run_only else compile_commands+[link]
    if not args.compile_only:jobs.append(run)
    for command in jobs:
        print('Running', command[command.index('-c') + 1] if '-c' in command else command[0], flush=True)
        subprocess.run(command, cwd=graph, env=env, stdout=log, stderr=subprocess.STDOUT,
                       check=True, timeout=240)
print('SPSM mesh-map suite',args.suite,'PASS' if not args.compile_only else 'compile PASS')
