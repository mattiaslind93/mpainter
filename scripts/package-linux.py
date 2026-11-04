#!/usr/bin/env python3
"""Stage a portable Linux tarball with Qt and recursively discovered ELF dependencies."""
import argparse, pathlib, shutil, subprocess, os, re, tarfile, json, sys
p=argparse.ArgumentParser();p.add_argument('--runtime',type=pathlib.Path,required=True);p.add_argument('--qt',type=pathlib.Path,required=True);p.add_argument('--output',type=pathlib.Path,required=True);p.add_argument('--no-archive',action='store_true');a=p.parse_args()
a.runtime=a.runtime.absolute();a.qt=a.qt.absolute()
root=pathlib.Path(__file__).absolute().parents[1];out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
def is_elf(path):
    if not path.is_file():return False
    with path.open('rb') as stream:return stream.read(4)==b'\x7fELF'
world=a.runtime/'5.2/datafiles/studiolights/world'
world.mkdir(parents=True,exist_ok=True)
for name in ('default-studio.hdr','default-studio.txt'):
    shutil.copy2(root/'assets/environments'/name,world/name)
subprocess.run([sys.executable,str(root/'scripts/check-environment.py'),str(a.runtime)],check=True)
stage=out/'mPainter-0.9.0-alpha-linux-x86_64'
if stage.exists():raise SystemExit('Output staging directory exists; choose a new output directory.')
stage.mkdir();(stage/'lib').mkdir()
shutil.copy2(a.runtime/'mpainter',stage/'mpainter')
shutil.copytree(a.runtime/'5.2',stage/'5.2',ignore=shutil.ignore_patterns('__pycache__','*.pyc'))
shutil.rmtree(stage/'5.2/datafiles/assets',ignore_errors=True)
# Cycles device kernels are portable release resources, not workstation caches.
kernels=stage/'5.2/scripts/addons_core/cycles/lib'
if not list(kernels.glob('kernel_sm_*.cubin*')) or not (kernels/'kernel_optix.ptx.zst').is_file():
    raise SystemExit('CUDA and OptiX kernels are required in the release runtime.')
# Blender's scene templates are not part of the painting host or its empty library.
shutil.rmtree(stage/'5.2/scripts/startup/bl_app_templates_system',ignore_errors=True)
# Resource-only packages must never inherit stock material libraries.
for directory in ['branding','meshes']:
    shutil.copytree(root/'assets'/directory,stage/('branding' if directory=='branding' else 'assets/meshes'))
subprocess.run([sys.executable,str(root/'scripts/sanitize-runtime.py'),str(stage/'5.2')],check=True)
plugins=stage/'plugins';plugins.mkdir()
for directory in ['platforms','imageformats','iconengines','platforminputcontexts','xcbglintegrations','wayland-decoration-client','wayland-graphics-integration-client','wayland-shell-integration','tls']:
    if (a.qt/'plugins'/directory).exists():shutil.copytree(a.qt/'plugins'/directory,plugins/directory)
if (a.runtime/'lib').exists():
    for directory in ['usd', 'materialx']:
        if (a.runtime/'lib'/directory).exists():
            shutil.copytree(a.runtime/'lib'/directory,stage/'lib'/directory,ignore=shutil.ignore_patterns('__pycache__','*.pyc'))
    for lib in (a.runtime/'lib').glob('*.so*'):
        # Optional Intel/AMD GPU adapters require separate Level Zero/HIP stacks.
        # Keep CPU/CUDA denoising and the core SYCL loader, but do not ship these adapters.
        if lib.name.startswith(('libcuda.so','libnvoptix.so','libnvidia','libur_adapter_level_zero','libOpenImageDenoise_device_sycl','libOpenImageDenoise_device_hip')):continue
        if lib.is_file():shutil.copy2(lib.resolve(),stage/'lib'/lib.name)
# The OS supplies glibc and graphics drivers/dispatch to match its display server.
external=re.compile(r'^(?:linux-vdso|ld-linux|lib(?:c|m|dl|rt|pthread|resolv|util)\.so|lib(?:GL|GLX|OpenGL|EGL|GLdispatch|cuda\.so|nvoptix\.so|nvidia))')
queue=[stage/'mpainter',*(p for p in stage.rglob('*.so*') if is_elf(p))];visited=set();origins={};external_libraries=set()
env=dict(os.environ);env['LD_LIBRARY_PATH']=str(a.qt/'lib')+':'+str(a.runtime/'lib')+':'+env.get('LD_LIBRARY_PATH','')
while queue:
    binary=queue.pop();key=str(binary.resolve())
    if key in visited:continue
    visited.add(key)
    module_env=dict(env,LD_LIBRARY_PATH=str(binary.parent)+':'+env['LD_LIBRARY_PATH'])
    result=subprocess.run(['ldd',str(binary)],env=module_env,text=True,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
    for line in result.stdout.splitlines():
        if '=> not found' in line:
            name=line.split('=>',1)[0].strip()
            if external.match(name):external_libraries.add(name);continue
            raise SystemExit('Unresolved dependency for '+str(binary)+': '+line)
        m=re.match(r'\s*(\S+) => (/\S+) \(',line)
        if not m:continue
        name,path=m.groups()
        if external.match(name):external_libraries.add(name);continue
        resolved=pathlib.Path(path).resolve()
        # Python extension modules may depend on one another; preserve their identity.
        if resolved.parent==binary.parent.resolve() and resolved.is_relative_to(stage):
            queue.append(resolved)
            continue
        dest=stage/'lib'/name
        if not dest.exists():shutil.copy2(pathlib.Path(path).resolve(),dest);queue.append(dest)
        origins[name]=path
for elf in [stage/'mpainter',*(p for p in stage.rglob('*.so*') if is_elf(p))]:
    # Normalize only Qt assertion-source strings, preserving ELF layout and source provenance.
    if elf.name.startswith('libQt6'):
        raw=elf.read_bytes()
        diagnostic=re.compile(rb'/[A-Za-z0-9_./-]+/qtbase-everywhere-src-[0-9.]+/(src/[A-Za-z0-9_./-]+\.cpp)\0')
        def relative_diagnostic(match):
            value=b'qtbase/'+match[1]
            return value+b'\0'*(len(match[0])-len(value))
        cleaned=diagnostic.sub(relative_diagnostic,raw)
        if cleaned!=raw:elf.write_bytes(cleaned)
    # ELF build IDs remain available; debug information and absolute source tables do not ship.
    subprocess.run(['strip','--strip-unneeded',str(elf)],check=True)
    relative=os.path.relpath(stage/'lib',elf.parent)
    subprocess.run(['patchelf','--set-rpath','$ORIGIN:$ORIGIN/'+relative,str(elf)],check=True)
# NVIDIA driver binaries must always come from the running host.
for library in stage.rglob('*.so*'):
    if library.name.startswith(('libcuda.so','libnvoptix.so','libnvidia')):
        raise SystemExit('NVIDIA driver library must not be bundled: '+str(library))
# Verify the completed package without access to the build SDK search paths.
for binary in [stage/'mpainter',*(p for p in stage.rglob('*.so*') if is_elf(p))]:
    result=subprocess.run(['ldd',str(binary)],env=dict(os.environ,LD_LIBRARY_PATH=str(stage/'lib')),capture_output=True,text=True)
    if 'version ' in result.stderr and 'not found' in result.stderr:raise SystemExit(result.stderr)
    for line in result.stdout.splitlines():
        if '=>' not in line:continue
        name,target=line.strip().split('=>',1);name=name.strip();target=target.strip().split(' (',1)[0]
        if external.match(name):continue
        if target=='not found' or not pathlib.Path(target).resolve().is_relative_to(stage):
            raise SystemExit('Dependency escaped the package: '+str(binary)+': '+line)
(stage/'AppRun').write_text('''#!/bin/sh
set -eu
app_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
driver_dir=
child=
cleanup() { [ -z "$driver_dir" ] || rm -rf -- "$driver_dir"; }
stop() { [ -z "$child" ] || kill -TERM "$child" 2>/dev/null || :; }
trap cleanup EXIT
trap stop INT TERM
# Distrobox exposes the host driver outside the container's linker paths.
# Link only NVIDIA drivers, never the host's libc or unrelated system libraries.
if [ -r /run/host/usr/lib64/libcuda.so.1 ] && [ ! -r /usr/lib64/libcuda.so.1 ]; then
    driver_dir=$(mktemp -d "${TMPDIR:-/tmp}/mpainter-drivers.XXXXXX")
    for library in /run/host/usr/lib64/libcuda.so* /run/host/usr/lib64/libnvoptix.so* /run/host/usr/lib64/libnvidia-ptxjitcompiler.so* /run/host/usr/lib64/libnvidia-gpucomp.so* /run/host/usr/lib64/libnvidia-rtcore.so*; do
        [ ! -f "$library" ] || ln -s "$library" "$driver_dir/${library##*/}"
    done
fi
export LD_LIBRARY_PATH="$app_dir/lib${driver_dir:+:$driver_dir}${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export QT_PLUGIN_PATH="$app_dir/plugins"
export QT_QPA_PLATFORM="${QT_QPA_PLATFORM:-xcb}"
export BLENDER_SYSTEM_SCRIPTS="$app_dir/5.2/scripts"
export BLENDER_SYSTEM_DATAFILES="$app_dir/5.2/datafiles"
if [ -z "$driver_dir" ]; then
    exec "$app_dir/mpainter" --workspace "$@"
fi
"$app_dir/mpainter" --workspace "$@" &
child=$!
wait "$child"
''');(stage/'AppRun').chmod(0o755)
(stage/'qt.conf').write_text('[Paths]\nPrefix=.\nLibraries=lib\nPlugins=plugins\n')
shutil.copy2(root/'packaging/se.mattiaslind.mpainter.desktop',stage/'se.mattiaslind.mpainter.desktop')
shutil.copy2(root/'packaging/se.mattiaslind.mpainter.metainfo.xml',stage/'se.mattiaslind.mpainter.metainfo.xml')
shutil.copy2(root/'assets/branding/mpainter-icon-256.png',stage/'se.mattiaslind.mpainter.png')
licenses=stage/'licenses';licenses.mkdir()
shutil.copy2(root/'assets/grunges/README.md',licenses/'mask-grunges.md')
shutil.copy2(root/'assets/mask-preview/README.md',licenses/'mask-preview.md')
shutil.copytree(root/'third_party/optix',licenses/'OptiX')
for name in ['LICENSE','NOTICE.md','README.md']:shutil.copy2(root/name,stage/name)
# Preserve the installed Blender third-party license inventory.
if (a.runtime/"licenses").exists():shutil.copytree(a.runtime/"licenses",licenses/"Blender")
if (a.runtime/"license").exists():shutil.copytree(a.runtime/"license",licenses/"Blender",dirs_exist_ok=True)
if (a.runtime/"dependencies.md").exists():shutil.copy2(a.runtime/"dependencies.md",licenses/"dependencies.md")
for item in a.runtime.iterdir():
    if item.is_file() and any(word in item.name.lower() for word in ['license','copyright','readme']):shutil.copy2(item,licenses/item.name)
qtlicenses=a.qt/'licenses'
if qtlicenses.exists():shutil.copytree(qtlicenses,licenses/'Qt')
shutil.copytree(root/'assets/file-browser',licenses/'file-browser',ignore=shutil.ignore_patterns('atlas.png','*.qrc','manifest.json','svg'))
# Fedora build: collect packaged license texts for every added system library.
packages=set()
if shutil.which('rpm'):
    for path in origins.values():
        result=subprocess.run(['rpm','-qf','--qf','%{NAME}',str(pathlib.Path(path).resolve())],capture_output=True,text=True)
        if result.returncode==0:packages.add(result.stdout.strip())
    for package in sorted(packages):
        result=subprocess.run(['rpm','-ql',package],capture_output=True,text=True)
        for source in result.stdout.splitlines():
            if source.startswith('/usr/share/licenses/') and pathlib.Path(source).is_file():
                dest=licenses/'system'/pathlib.Path(source).relative_to('/usr/share/licenses');dest.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(source,dest)
(stage/'BUILD-INFO.txt').write_text('mPainter 0.9.0-alpha\nBlender 5.2.0\nQt 6.8.3\nRequires a compatible Linux glibc and OpenGL 4.3 graphics driver.\nCycles CUDA (sm_86 and forward-compatible PTX) and OptiX kernels are included.\nCUDA and OptiX use the system NVIDIA driver, which is never bundled; CPU fallback is included.\nOptional Intel/AMD GPU denoising backends are not bundled.\nSource preparation and package commands: docs/building.md in the matching source tree.\n')
dependency_manifest=json.dumps({'libraries':sorted(origins),'external_libraries':sorted(external_libraries),'system_packages':sorted(packages)},indent=2)+'\n'
(out/'linux-dependencies.json').write_text(dependency_manifest)
(stage/'DEPENDENCIES.json').write_text(dependency_manifest)
if not a.no_archive:
    archive=out/(stage.name+'.tar.gz')
    with tarfile.open(archive,'w:gz',compresslevel=6) as tar:tar.add(stage,arcname=stage.name)
    print(archive)
else:print(stage)
