"""Compile one patched QtWidgets TU and relink into a private validation stage.

Existing Qt build artifacts/source are read-only inputs. This never invokes an
SDK install or ninja rebuild and never changes an artist's Qt prefix.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shlex
import shutil
import subprocess
import time

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--qt-source', required=True, type=Path)
parser.add_argument('--qt-build', required=True, type=Path)
parser.add_argument('--output', required=True, type=Path)
args = parser.parse_args()
source, build, output = (p.resolve() for p in (args.qt_source, args.qt_build, args.output))
if output == source or output == build or output in source.parents or output in build.parents or source in output.parents or build in output.parents:
    raise SystemExit('Output must be separate from the existing Qt source/build')
prefix_line = next(line for line in (build / 'CMakeCache.txt').read_text().splitlines() if line.startswith('CMAKE_INSTALL_PREFIX:PATH='))
prefix = Path(prefix_line.split('=', 1)[1]).resolve()
if output == prefix or prefix in output.parents or output in prefix.parents:
    raise SystemExit('Prototype output cannot replace or overlap the installed Qt SDK')
def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()
inputs = [source / 'src/widgets/dialogs/qfiledialog.cpp', prefix / 'lib/libQt6Widgets.so.6.8.3']
before = {str(path): digest(path) for path in inputs}
scripts = Path(__file__).resolve().parent
patched = output / 'source/src/widgets/dialogs/qfiledialog.cpp'
patched.parent.mkdir(parents=True, exist_ok=True)
shutil.copy2(source / 'src/widgets/dialogs/qfiledialog.cpp', patched)
subprocess.run(['python3', str(scripts / 'apply-qt-patches.py'), '--source', str(output / 'source')], check=True)
ninja = shutil.which('ninja-build') or shutil.which('ninja')
commands = subprocess.check_output([ninja, '-t', 'commands', 'Widgets'], cwd=build, text=True).splitlines()
object_name = 'src/widgets/CMakeFiles/Widgets.dir/dialogs/qfiledialog.cpp.o'
compile_command = next(c for c in commands if f' -o {object_name} -c ' in c)
library_name = 'lib/libQt6Widgets.so.6.8.3'
link_command = next(c for c in commands if f' -o {library_name} ' in c and ' -shared ' in c)
object_file = output / 'qfiledialog.cpp.o'
argv = shlex.split(compile_command)
argv[argv.index('-o') + 1] = str(object_file)
argv[argv.index('-c') + 1] = str(patched)
if '-MF' in argv:
    argv[argv.index('-MF') + 1] = str(object_file) + '.d'
start = time.monotonic()
subprocess.run(['nice', '-n', '19', *argv], cwd=build, check=True)
compile_seconds = time.monotonic() - start
print(f'Qt file-dialog TU compile: {compile_seconds:.2f}s', flush=True)
library = output / library_name
library.parent.mkdir(parents=True, exist_ok=True)
link_argv = shlex.split(link_command)
if link_argv[:2] == [':', '&&'] and link_argv[-2:] == ['&&', ':']:
    link_argv = link_argv[2:-2]
if any(token in ('&&', ';', '|') for token in link_argv):
    raise SystemExit('Unsupported Qt linker shell pipeline')
link_argv[link_argv.index('-o') + 1] = str(library)
link_argv = [str(object_file) if token == object_name else
             '-Wl,-rpath,$ORIGIN' if token.startswith('-Wl,-rpath,') else token
             for token in link_argv]
start = time.monotonic()
subprocess.run(['nice', '-n', '19', *link_argv], cwd=build, check=True)
link_seconds = time.monotonic() - start
print(f'QtWidgets private relink: {link_seconds:.2f}s', flush=True)
for name in ('libQt6Widgets.so.6', 'libQt6Widgets.so'):
    link = library.parent / name
    if link.is_symlink():
        link.unlink()
    link.symlink_to(library.name)
after = {str(path): digest(path) for path in inputs}
if before != after:
    raise SystemExit('An original Qt input changed during the private build')
dynamic = subprocess.check_output(['readelf', '-d', str(library)], text=True)
runpath = next(line for line in dynamic.splitlines() if '(RUNPATH)' in line)
if runpath.rsplit('[', 1)[-1].rstrip(']') != '$ORIGIN':
    raise SystemExit('Private QtWidgets must have install-equivalent RUNPATH=$ORIGIN')
(output / 'build-proof.json').write_text(json.dumps({'input_sha256': before, 'original_inputs_unchanged': True,
    'compile_seconds': compile_seconds, 'link_seconds': link_seconds,
    'runpath': '$ORIGIN', 'private_library_sha256': digest(library)}, indent=2) + '\n')
print(f'Private validation library: {library}')
