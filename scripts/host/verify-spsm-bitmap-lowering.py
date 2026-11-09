# SPDX-License-Identifier: GPL-3.0-or-later
"""Compile/run the CPU SPSM bitmap lowering against existing native host libraries.

Only private objects/executables are written. No host build or GPU context runs.
"""
import argparse
import os
from pathlib import Path
import shlex
import shutil
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--build-dir', type=Path, required=True)
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--fixtures', type=Path)
parser.add_argument('--sanitize', action='store_true')
parser.add_argument('--runtime-baseline', action='store_true')
parser.add_argument('--lsan-suppressions', type=Path)
args = parser.parse_args()
build, output = args.build_dir.resolve(), args.output.resolve()
output.mkdir(parents=True, exist_ok=True)
root = Path(__file__).resolve().parents[2]
ninja = shutil.which('ninja') or shutil.which('ninja-build')
commands = subprocess.check_output([ninja, '-t', 'commands', 'bts-host'], cwd=build, text=True).splitlines()
base = shlex.split(next(c for c in commands if '/main.cc.o' in c and ' -c ' in c))
main_object = base[base.index('-o') + 1]
sources = ['native/host/SpsmBitmapTexture.cc', 'native/spsm/SpsmReader.cc',
           'native/spsm/bitmaps/SpsmBitmap.cc', 'native/spsm/semantic/SpsmSemantic.cc', 'native/host/SpsmMaterialImport.cc',
           'native/host/SpsmBitmapLowering.cc', 'native/tests/spsm_bitmap_lowering_test.cc']
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
link[1:1] = objects
executable = output / 'spsm-bitmap-lowering-test'
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
with (output / 'run.log').open('w') as log:
    for command in compile_commands + [link, run]:
        print('Running', command[command.index('-c') + 1] if '-c' in command else command[0], flush=True)
        subprocess.run(command, cwd=build, env=env, stdout=log, stderr=subprocess.STDOUT,
                       check=True, timeout=240)
print('SPSM bitmap lowering CPU proof PASS (no GPU context)')
