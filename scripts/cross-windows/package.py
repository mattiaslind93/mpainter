#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Stage the cross-built Windows runtime locally; check imports before archiving.

Run after build.sh inside the container with env.sh sourced. No network access,
publication, or upload is performed. Existing output is never overwritten.
Output goes to MP_WIN_DIST, by default $MP_WIN_WORK/dist/candidates/<today>.

The runtime must have been built from this checkout's HEAD. To exercise the
scripts against an older runtime, set MP_WIN_PACKAGING_PROOF=1. The stage is
then named and labelled as a packaging proof, not a release.
"""
from datetime import date
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import zipfile

work = Path(os.environ['MP_WIN_WORK'])
qt = Path(os.environ['MP_WIN_QT'])
repo = Path(__file__).absolute().parents[2]
runtime = work / 'build/windows/native/bin'
libroot = work / 'build/windows/lib-windows_x64'
version = re.search(r'^version = "([^"]+)"', (repo / 'pyproject.toml').read_text(), re.M).group(1)
# build.sh compiles the snapshot recorded here, not whatever this checkout holds now.
built_commit = (work / 'build/windows/source-head.txt').read_text().strip()
packaging_commit = subprocess.check_output(['git', '-C', str(repo), 'rev-parse', 'HEAD'],
                                           text=True).strip()
proof = os.environ.get('MP_WIN_PACKAGING_PROOF') == '1'
if built_commit != packaging_commit and not proof:
    raise SystemExit(f'Runtime was built from {built_commit} but this checkout is '
                     f'{packaging_commit}; rebuild, or set MP_WIN_PACKAGING_PROOF=1')
dist = Path(os.environ.get('MP_WIN_DIST') or work / 'dist/candidates' / date.today().isoformat())
if dist.resolve() == (work / 'dist').resolve():
    raise SystemExit('Write new packages under dist/candidates/, not the dist/ root')
name = f'mPainter-{version}-windows-x64' + ('-packaging-proof' if proof else '')
stage = dist / name
archive = dist / (name + '.zip')
checksum = archive.with_suffix('.zip.sha256')
logs = work / 'logs/windows2'
logs.mkdir(parents=True, exist_ok=True)
if not (runtime / 'mPainter.exe').is_file() or not (runtime / '5.2').is_dir():
    raise SystemExit('Build and install mPainter and the Blender runtime first')
if any(path.exists() for path in (stage, archive, checksum)):
    raise SystemExit('Output already exists; preserve/move it before packaging again')
stage.mkdir(parents=True)


def case_alias(path):
    if not path.is_symlink():
        return False
    target = path.parent / os.readlink(path)
    return (target.exists() and target.name != path.name
            and target.name.casefold() == path.name.casefold()
            and target.parent.resolve() == path.parent.resolve())


def ignore_build_files(directory, names):
    # The Linux cross-build needs case aliases; a Windows archive must contain
    # only the canonical sibling. Do not traverse aliases of whole directories.
    return [name for name in names if name.lower().endswith('.pdb')
            or name == '__pycache__' or case_alias(Path(directory) / name)]


def copy_file(source, target):
    source = Path(source)
    # CMake installs case aliases as relative symlinks even when their real
    # filename is not also installed. Resolve only known library DLL payloads.
    if source.is_symlink() and not source.exists():
        candidates = [p for p in libroot.rglob('*.dll')
                      if not p.is_symlink()
                      and p.name.casefold() == source.name.casefold()]
        if len(candidates) != 1:
            raise RuntimeError(f'Cannot uniquely resolve installed DLL alias: {source}')
        print(f'Materialize {source.name} from {candidates[0]}', flush=True)
        source = candidates[0]
    return shutil.copy2(source, target)


def copy(source, target):
    if source.is_dir():
        shutil.copytree(source, target, dirs_exist_ok=True, copy_function=copy_file,
                        ignore=ignore_build_files)
    else:
        target.parent.mkdir(parents=True, exist_ok=True)
        copy_file(source, target)


copy(runtime / 'mPainter.exe', stage / 'mPainter.exe')
for folder in ('5.2', 'blender.shared', 'lib'):
    if (runtime / folder).is_dir():
        copy(runtime / folder, stage / folder)
for path in runtime.glob('*.dll'):
    if not case_alias(path):
        copy(path, stage / path.name)
for relative in ('5.2/datafiles/assets', '5.2/scripts/addons_core/cycles/lib',
                 '5.2/scripts/startup/bl_app_templates_system'):
    if (stage / relative).is_dir():
        shutil.rmtree(stage / relative)
# pip/setuptools bundle launcher templates for several architectures. This
# runtime is x64 only; retain its launchers and omit unused x86/ARM templates.
site_packages = stage / '5.2/python/lib/site-packages'
for folder, names in {
    'pip/_vendor/distlib': ('t32.exe', 'w32.exe', 't64-arm.exe', 'w64-arm.exe'),
    'setuptools': ('cli.exe', 'gui.exe', 'cli-32.exe', 'gui-32.exe',
                   'cli-arm64.exe', 'gui-arm64.exe'),
}.items():
    for filename in names:
        (site_packages / folder / filename).unlink(missing_ok=True)

subprocess.run([sys.executable, str(repo / 'scripts/sanitize-runtime.py'),
                str(stage / '5.2')], check=True)
for filename in ('default-studio.hdr', 'default-studio.txt'):
    copy(repo / 'assets/environments' / filename,
         stage / '5.2/datafiles/studiolights/world' / filename)
subprocess.run([sys.executable, str(repo / 'scripts/check-environment.py'), str(stage)], check=True)
copy(repo / 'assets/branding', stage / 'branding')
copy(repo / 'assets/meshes', stage / 'assets/meshes')
copy(repo / 'assets/materials', stage / 'assets/materials')
for filename in ('LICENSE', 'NOTICE.md', 'README.md'):
    copy(repo / filename, stage / filename)

# Deploy the Qt modules and plugins this Widgets/OpenGL application uses.
# Recursive PE closure below adds each plugin's required Qt/dependency DLLs.
for module in ('Core', 'Gui', 'Widgets', 'OpenGL', 'OpenGLWidgets'):
    copy(qt / f'bin/Qt6{module}.dll', stage / f'Qt6{module}.dll')
for plugin in ('qwindows.dll', 'qoffscreen.dll'):
    copy(qt / 'plugins/platforms' / plugin, stage / 'platforms' / plugin)
for folder in ('imageformats', 'iconengines'):
    for plugin in (qt / 'plugins' / folder).glob('*.dll'):
        if not plugin.stem.endswith('d'):
            copy(plugin, stage / folder / plugin.name)
# Copied by name: the debug filter above would drop it, since "backend" ends in "d".
# Schannel uses the Windows TLS stack; the OpenSSL backend would need OpenSSL DLLs.
copy(qt / 'plugins/tls/qschannelbackend.dll', stage / 'tls/qschannelbackend.dll')
(stage / 'qt.conf').write_text('[Paths]\nPlugins=.\n')
for path in (work / 'toolchains/windows/redist').glob('*.dll'):
    copy(path, stage / path.name)

# Root copies make dependencies available to the host and Python/Qt plugins
# without relying on the standalone Blender executable's activation manifest.
shared = stage / 'blender.shared'
if shared.is_dir():
    for path in shared.glob('*.dll'):
        copy(path, stage / path.name)

licenses = stage / 'licenses'
for folder in ('license', 'licenses'):
    if (runtime / folder).is_dir():
        copy(runtime / folder, licenses / 'Blender')
for path in runtime.iterdir():
    if (path.is_file() and not case_alias(path)
            and any(word in path.name.lower() for word in ('license', 'copyright', 'dependencies'))):
        copy(path, licenses / path.name)
for folder in ('licenses', 'sbom'):
    if (qt / folder).is_dir():
        copy(qt / folder, licenses / 'Qt' / folder)
# The aqt Windows binary archive omits license texts. The matching, retained
# Qt 6.8.3 host installation includes the upstream license collection.
if not (qt / 'licenses').is_dir():
    copy(work / 'toolchains/qt/licenses', licenses / 'Qt/licenses')
for dependency in (work / 'build/windows/dependency-source').iterdir():
    if dependency.is_dir():
        for path in dependency.iterdir():
            if path.is_file() and path.name.lower().startswith(('license', 'copying', 'copyright')):
                copy(path, licenses / dependency.name / path.name)
copy(libroot / 'deps.md', licenses / 'blender-dependencies.md')
for source, target in (('assets/grunges/README.md', 'mask-grunges.md'),
                       ('assets/mask-preview/README.md', 'mask-preview.md'),
                       ('assets/meshes/Shader_Ball_JL_01-NOTICE.md', 'shaderball-jl01.md'),
                       ('assets/meshes/CC-BY-4.0.txt', 'CC-BY-4.0.txt')):
    copy(repo / source, licenses / target)
shutil.copytree(repo / 'assets/file-browser', licenses / 'file-browser',
                ignore=shutil.ignore_patterns('atlas.png', '*.qrc', 'manifest.json', 'svg'))
if (repo / 'third_party/optix').is_dir():
    copy(repo / 'third_party/optix', licenses / 'OptiX')

build_info = [f'mPainter {version}', f'Commit {built_commit}', 'Blender 5.2.0', 'Qt 6.8.3',
              'Windows x64, cross-built on Linux with clang-cl for the MSVC ABI.',
              'Requires an OpenGL 4.3 graphics driver.',
              'HTTPS uses the Qt Schannel TLS backend in tls/qschannelbackend.dll. '
              'No OpenSSL DLLs are bundled.',
              'Source preparation and package commands: scripts/cross-windows/README.md '
              'in the matching source tree.']
if proof:
    build_info.insert(2, 'PACKAGING PROOF, NOT A RELEASE. The runtime was built from '
                         f'{built_commit}. These packaging scripts are from {packaging_commit}.')
(stage / 'BUILD-INFO.txt').write_text('\n'.join(build_info) + '\n')

search = [runtime, runtime / 'blender.shared', qt / 'bin',
          work / 'toolchains/windows/deps/bin', work / 'toolchains/windows/redist',
          libroot / 'vulkan/bin', libroot / 'level-zero/bin']
closure = logs / 'app-dll-closure.json'
added = []
while True:
    closure.unlink(missing_ok=True)
    result = subprocess.run([sys.executable, str(repo / 'scripts/cross-windows/check-pe.py'),
                             str(stage), '--allow-optional-gpu-drivers', '--output', str(closure)])
    if result.returncode == 0:
        break
    if not closure.is_file():
        raise SystemExit('DLL checker failed before producing its report; see diagnostic above')
    missing = json.loads(closure.read_text())['unresolved']
    if not missing:
        raise SystemExit('DLL checker failed without an import diagnostic')
    for name in sorted({entry['import'] for entry in missing}):
        candidates = [p for directory in search for p in directory.glob('*')
                      if p.is_file() and p.name.casefold() == name.casefold()]
        if not candidates:
            raise SystemExit(f'Unresolved DLL: {name}; see {closure}')
        destination = stage / name
        if destination.exists():
            raise SystemExit(f'DLL already staged but unresolved: {name}')
        copy(candidates[0], destination)
        added.append({'dll': name, 'source': str(candidates[0])})
(logs / 'package-added-dlls.json').write_text(json.dumps(added, indent=2) + '\n')

windows_paths = {}
for path in stage.rglob('*'):
    relative = path.relative_to(stage).as_posix()
    key = relative.casefold()
    if key in windows_paths:
        raise SystemExit(f'Windows case collision: {windows_paths[key]} / {relative}')
    if path.is_symlink():
        raise SystemExit(f'Unexpected packaged symlink: {relative}')
    windows_paths[key] = relative

with zipfile.ZipFile(archive, 'w', compression=zipfile.ZIP_DEFLATED, compresslevel=6) as output:
    for path in sorted(stage.rglob('*')):
        if path.is_file():
            output.write(path, path.relative_to(stage).as_posix())
with archive.open('rb') as stream:
    digest = hashlib.file_digest(stream, 'sha256').hexdigest()
checksum.write_text(f'{digest}  {archive.name}\n')
print(json.dumps({'artifact': str(archive), 'bytes': archive.stat().st_size,
                  'sha256': digest}, indent=2))
