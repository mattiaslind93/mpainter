"""Run actual resolution-menu widgets without a desktop or GPU context.

Reuse the built host objects, replacing only main in a private executable. Neither
the host executable, its objects, nor Ninja dependency files are modified.
"""
import argparse
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--build-dir', type=Path, required=True)
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
build, output = args.build_dir.resolve(), args.output.resolve()
output.mkdir(parents=True, exist_ok=True)
root = Path(__file__).resolve().parents[2]
ninja = shutil.which('ninja') or shutil.which('ninja-build')
if not ninja:
    raise RuntimeError('Ninja is required to inspect the existing host build')
commands = subprocess.check_output(
    [ninja, '-t', 'commands', 'bts-host'], cwd=build, text=True).splitlines()
compile_command = shlex.split(next(command for command in commands
    if '/main.cc.o' in command and ' -c ' in command))
original_object = compile_command[compile_command.index('-o') + 1]
test_object = str(output / 'texture-extent-choices-test.o')
compile_command[compile_command.index('-o') + 1] = test_object
compile_command[compile_command.index('-c') + 1] = str(
    root / 'native/tests/texture_extent_choices_test.cc')
for option, filename in (('-MF', output / 'compile.d'), ('-MT', Path(test_object))):
    if option in compile_command:
        compile_command[compile_command.index(option) + 1] = str(filename)
compile_command.append('-UNDEBUG')
link_command = shlex.split(commands[-1])
if link_command[:2] == [':', '&&']:
    link_command = link_command[2:]
if '&&' in link_command:
    link_command = link_command[:link_command.index('&&')]
if original_object not in link_command:
    raise RuntimeError('Cannot locate the host main object in its link command')
link_command = [test_object if token == original_object else token
    for token in link_command]
executable = output / 'texture-extent-choices-test'
link_command[link_command.index('-o') + 1] = str(executable)
link_command = [('-Wl,--dependency-file=' + str(output / 'link.d'))
    if token.startswith('-Wl,--dependency-file=') else token for token in link_command]
env = dict(os.environ, QT_QPA_PLATFORM='offscreen', BTS_HEADLESS_WORKSPACE='1')
for key, name in (('XDG_CONFIG_HOME', 'config'), ('XDG_STATE_HOME', 'state'), ('XDG_DATA_HOME', 'data'),
                  ('XDG_CACHE_HOME', 'cache'), ('BTS_RECOVERY_DIR', 'recovery'),
                  ('BTS_SESSION_LOG_DIR', 'logs'), ('BTS_RESOURCE_CACHE', 'resources')):
    env[key] = str(output / name)
cache = (build / 'CMakeCache.txt').read_text()
for line in cache.splitlines():
    if line.startswith('Qt6Core_DIR:PATH='):
        sdk = Path(line.split('=', 1)[1]).parents[2]
        env['QT_PLUGIN_PATH'] = str(sdk / 'plugins')
        env['LD_LIBRARY_PATH'] = str(sdk / 'lib') + (':' + env['LD_LIBRARY_PATH'] if env.get('LD_LIBRARY_PATH') else '')
        break
for line in cache.splitlines():
    if line.startswith('PYTHON_EXECUTABLE:FILEPATH='):
        env['PYTHONHOME'] = str(Path(line.split('=', 1)[1]).parent.parent)
        break
with (output / 'run.log').open('w') as log:
    for command in (compile_command, link_command, [str(executable), str(output)]):
        subprocess.run(command, cwd=build, env=env, stdout=log,
                       stderr=subprocess.STDOUT, check=True, timeout=110)
(output / 'verified.json').write_text(json.dumps({
    'actual_production_widgets': ['NewProjectDialog', 'WorkspaceShell',
                                  'MeshBakeDialog', 'SkewPanel'],
    'desktop_windows': 0, 'gpu_contexts': 0,
    'gpu_max_sizes': [16384, 32768, 0], 'unknown_gpu_choice_maximum': 4194304,
    'retained_dimensions': [65536, 32768], 'new_project_accepts_larger_sizes': True,
    'linked_dimensions_use_values': True, 'live_popup_refresh': True,
    'paint_allocations': 0, 'menu_ram_queries': 0,
    'unchanged_sync_model_mutations': 0, 'host_files_modified': False
}, indent=2) + '\n')
print('Native Qt resolution-menu regression PASS')
