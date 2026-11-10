# SPDX-License-Identifier: GPL-3.0-or-later
"""Focused normal-image CPU/GPU tests against immutable native link inputs.

The default CPU suite never creates a GPU context. The optional GPU suite uses
an isolated offscreen NVIDIA context. Run inside the build container at
nice 19; only private output files are written. No artist build is started.
"""
import argparse
import json
import os
from pathlib import Path
import shlex
import subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--host-snapshot', type=Path, required=True)
p.add_argument('--output', type=Path, required=True)
p.add_argument('--suite', choices=['cpu', 'gpu'], default='cpu')
p.add_argument('--baseline', action='store_true')
p.add_argument('--runtime-baseline', action='store_true')
p.add_argument('--sanitize', action='store_true')
p.add_argument('--lsan-suppressions', type=Path)
p.add_argument('--compile-only', action='store_true')
p.add_argument('--run-only', action='store_true')
a = p.parse_args()
if a.baseline and a.runtime_baseline:
    raise RuntimeError('Select at most one baseline mode')
root = Path(__file__).resolve().parents[2]
snapshot, output = a.host_snapshot.resolve(), a.output.resolve()
if not output.is_relative_to(root.parent / 'build') or output == snapshot:
    raise RuntimeError('Output must be a private directory under workspace build')
output.mkdir(parents=True, exist_ok=True)
commands = json.loads((snapshot / 'frozen-commands.json').read_text())
frozen = json.loads((snapshot / 'frozen-link-inputs.json').read_text())
base = shlex.split(next(c for c in commands if '/main.cc.o' in c and ' -c ' in c))
original_main = base[base.index('-o') + 1]
sources = ['native/host/blender/NativeMaterialImageResource.cc', 'native/host/blender/NativeMaterialImages.cc',
           'native/tests/native_material_image_color' + ('_gpu' if a.suite == 'gpu' else '') + '_test.cc']
if a.suite == 'gpu':
    if a.baseline or a.runtime_baseline or a.sanitize:
        raise RuntimeError('Use CPU suite for baseline/sanitizers')
    sources += ['native/host/NativeMaterialResource.cc', 'native/host/NativeMaterialArchive.cc',
                'native/host/BlendMaterial.cc']
if (root / 'native/host/NativeMaterialDecodedImage.cc').is_file():
    sources.append('native/host/NativeMaterialDecodedImage.cc')
flags = ['-fsanitize=address,undefined', '-fno-omit-frame-pointer'] if a.sanitize else []
objects, compiles = [], []
for source in sources:
    obj = str(output / (Path(source).stem + '.o'))
    command = base.copy()
    command[command.index('-o') + 1] = obj
    command[command.index('-c') + 1] = str(root / source)
    for option, value in [('-MF', obj + '.d'), ('-MT', obj)]:
        if option in command:
            command[command.index(option) + 1] = value
    insert = 2 if Path(command[0]).name == 'ccache' else 1
    command[insert:insert] = ['-I' + str(root / 'native/host')]
    command += ['-O1', '-g0', '-UNDEBUG'] + flags
    compiles.append(command)
    objects.append(obj)
link = shlex.split(commands[-1])
if link[:2] == [':', '&&']:
    link = link[2:]
if '&&' in link:
    link = link[:link.index('&&')]
link = [x for x in link if x != original_main and not any(
    x.endswith('/' + Path(source).name + '.o') for source in sources)]
for i, token in enumerate(link):
    if token.startswith('bin/lib/') and '.so' in token:
        private = snapshot / 'lib' / Path(token).name
        if not private.is_file():
            raise RuntimeError('Missing frozen dynamic host library: ' + str(private))
        link[i] = str(private)
        continue
    if token.endswith(('.o', '.a')):
        if token not in frozen:
            # Bundled third-party archives are immutable dependencies, not artist
            # build outputs. Every mutable host/Blender object must be frozen.
            if Path(token).is_absolute() and Path(token).resolve().is_relative_to((root.parent / 'blender/lib').resolve()):
                continue
            raise RuntimeError('No frozen mapping for mutable link input: ' + token)
        link[i] = frozen[token]
executable = output / ('native-image-color-' + a.suite + '-test')
link[1:1] = objects
link[link.index('-o') + 1] = str(executable)
link = ['-Wl,--dependency-file=' + str(output / 'link.d')
        if x.startswith('-Wl,--dependency-file=') else x for x in link]
link += flags
env = dict(os.environ, QT_QPA_PLATFORM='offscreen',
           OCIO=str(root.parent / 'blender/release/datafiles/colormanagement/config.ocio'))
sdk = root.parent / 'qt/6.8.3'
env.update(QT_PLUGIN_PATH=str(sdk / 'plugins'),
           LD_LIBRARY_PATH=str(sdk / 'lib') + ':' + str(snapshot / 'lib') + ':' + env.get('LD_LIBRARY_PATH', ''))
for variable, directory in [('XDG_CONFIG_HOME', 'config'), ('XDG_DATA_HOME', 'data'),
                            ('XDG_CACHE_HOME', 'cache'), ('BTS_RECOVERY_DIR', 'recovery'),
                            ('BTS_RESOURCE_CACHE', 'resources'), ('TMPDIR', 'tmp')]:
    (output / directory).mkdir(exist_ok=True)
    env[variable] = str(output / directory)
if a.sanitize:
    env.update(ASAN_OPTIONS='detect_leaks=1:halt_on_error=1', UBSAN_OPTIONS='halt_on_error=1')
if a.lsan_suppressions:
    env['LSAN_OPTIONS'] = 'suppressions=' + str(a.lsan_suppressions.resolve()) + ':print_suppressions=1'
run = [str(executable), str(output / 'fixtures')] + (['--baseline'] if a.baseline else ['--runtime-baseline'] if a.runtime_baseline else [])
if a.suite == 'gpu':
    env.update(BTS_ROOT=str(root.parent), BTS_HEADLESS_WORKSPACE='1')
    if not a.compile_only:
        for directory in ['lib', '5.2']:
            destination = output / directory
            if not destination.exists():
                subprocess.run(['cp', '-a', '--reflink=auto', str(snapshot / directory), str(destination)], check=True)
        run = ['bash', '-c', 'source "$1"; shift; exec "$@"', 'native-image-color',
               str(root / 'scripts/host/gpu-env.sh'), *run]
with (output / 'run.log').open('w') as log:
    for command in ([] if a.run_only else compiles + [link]) + ([] if a.compile_only else [run]):
        print('Running', command[command.index('-c') + 1] if '-c' in command else command[0], flush=True)
        subprocess.run(command, env=env, cwd=snapshot, stdout=log, stderr=subprocess.STDOUT,
                       check=True, timeout=240)
print('Normal native image color', a.suite, 'proof PASS' if not a.compile_only else 'driver compile PASS')
