#!/usr/bin/env python3
"""Configure and build the native host with upstream precompiled libraries."""
import argparse, collections, os, pathlib, platform, re, shutil, subprocess
p=argparse.ArgumentParser()
p.add_argument('--blender',required=True,type=pathlib.Path)
p.add_argument('--build',required=True,type=pathlib.Path)
p.add_argument('--qt',required=True,type=pathlib.Path)
p.add_argument('--jobs',default=4,type=int)
p.add_argument('--toolchain',type=pathlib.Path)
p.add_argument('--cuda',type=pathlib.Path,help='CUDA toolkit root (compiler only; driver libraries are never packaged)')
p.add_argument('--optix',type=pathlib.Path,help='OptiX SDK root containing include/optix.h')
p.add_argument('--cuda-architectures',default='sm_86;compute_75',help='Native RTX 30 kernel and forward-compatible PTX')
a=p.parse_args();root=pathlib.Path(__file__).absolute().parents[1]

def default_gcc_too_old():
    """Blender 5.2 refuses any GCC older than 14. Ubuntu 24.04 still installs 13
    as the default cc, so configure stopped on the version check before a single
    file was compiled. Only a too-old GCC needs replacing; clang is left alone."""
    try:
        if 'clang' in subprocess.run(['cc','--version'],capture_output=True,text=True,check=True).stdout.lower():return False
        return int(subprocess.run(['cc','-dumpversion'],capture_output=True,text=True,check=True).stdout.split('.')[0])<14
    except Exception:return False

def compiler_args():
    """An explicit CC/CXX is the caller's choice and wins. Otherwise pick the
    newest installed compiler the pinned source accepts, and say so plainly
    rather than letting CMake fail three minutes in."""
    if platform.system()!='Linux' or os.environ.get('CC') or os.environ.get('CXX') or not default_gcc_too_old():return []
    for names in [('gcc-%d','g++-%d',range(20,13,-1)),('clang-%d','clang++-%d',range(20,15,-1))]:
        for major in names[2]:
            c,cxx=shutil.which(names[0]%major),shutil.which(names[1]%major)
            if c and cxx:return ['-DCMAKE_C_COMPILER='+c,'-DCMAKE_CXX_COMPILER='+cxx]
    raise SystemExit('No compiler new enough for the pinned Blender source. Install gcc-14 and g++-14 (Ubuntu: apt-get install gcc-14 g++-14), clang 16 or newer, or point CC and CXX at one.')

# CUDA kernels need nvcc and OptiX needs its SDK headers; asking for either
# without the toolkit installed stops configure outright. The switches follow
# what is actually present so a machine without an NVIDIA toolchain still
# builds, with GPU rendering left out rather than the whole build refused.
has_cuda=bool(a.cuda) or bool(shutil.which('nvcc'))
cuda_kernels='ON' if platform.system()=='Linux' and has_cuda else 'OFF'
optix_device='ON' if platform.system()=='Linux' and a.optix else 'OFF'
def install_only_targets(build):
    """Blender installs files that nothing in our chosen targets produces: the
    glTF add-on's Draco and meshopt bridges, Cycles' compiled OSL shaders, and
    whatever upstream adds next. A build limited to bts-host and blender leaves
    them out and the install step then stops on the first one missing, after
    hours of compiling. Rather than name them here, ask the build graph: CMake
    gives every target a phony alias listing what it produces, so each file the
    install scripts copy out of the build tree can be traced back to the target
    that makes it. Targets absent from this configuration simply never come up.
    """
    graph=build/'build.ninja'
    if not graph.exists():return []
    text=graph.read_text(errors='replace').replace('$\n',' ')
    aliases=[(name.strip(),produced.split()) for name,produced in
             re.findall(r'(?m)^build ([^:$\n]+): phony ([^\n]*)',text)]
    owners=collections.defaultdict(list);plain={}
    for name,produced in aliases:
        for item in produced:owners[item].append(name)
        # A target's own alias is its bare name; the one CMake writes per
        # directory points at it, so one hop reaches the name to build.
        if '/' not in name:
            for item in produced:plain.setdefault(item,name)
    def target_name(alias):
        alias=plain.get(alias,alias)
        # CMake's ordering and bookkeeping helpers are not targets to build.
        if '/' in alias or alias.startswith('cmake_object_order_depends_target_'):return None
        return alias
    wanted=[]
    def want(name):
        if name and name not in wanted:wanted.append(name)
    prefix=re.escape(str(build))
    for script in sorted(build.rglob('cmake_install.cmake')):
        body=script.read_text(errors='replace')
        for path in re.findall('"('+prefix+r'/[^"]+)"',body):
            for alias in owners.get(str(pathlib.Path(path).relative_to(build)),[]):
                name=target_name(alias)
                if name:want(name);break
        # Shared modules are also matched by name, so a library installed
        # through a path this does not recognise is still not missed.
        for stem in re.findall(r'([A-Za-z0-9_.\-]+)\.(?:so|dylib|dll)',body):
            for candidate in (stem,stem[3:] if stem.startswith('lib') else stem):
                if any(candidate==name for name,_ in aliases):want(candidate)
    return wanted

def report_unparsable_ninja(build):
    """One stray dollar makes ninja refuse the entire graph, and all it reports
    is a line number, which tells you nothing from a CI log. Print the lines it
    would reject. ninja reads $$, $:, an escaped space, a line continuation,
    ${name} and $name; a dollar before anything else is the error."""
    graph=build/'build.ninja'
    if not graph.exists():return
    stray=re.compile(r'\$(?![$:{ \t\r\n]|[A-Za-z0-9_.-])')
    for number,line in enumerate(graph.read_text(errors='replace').splitlines(),1):
        if stray.search(line):print('build.ninja:%d: %s'%(number,line[:500]),flush=True)

if platform.system()=='Darwin':
    raise SystemExit('macOS viewport unavailable: this host requires OpenGL 4.3; Blender 5.2 uses Metal on macOS. A Metal/Qt presentation port is required before packaging.')
args=['cmake','-S',str(a.blender.absolute()),'-B',str(a.build.absolute()),'-G','Ninja',
      '-DCMAKE_BUILD_TYPE=Release','-DCMAKE_PREFIX_PATH='+str(a.qt.absolute()),
      '-DBTS_HOST_SOURCE_DIR='+str(root/'native/host'),
      '-DCMAKE_INSTALL_PREFIX='+str(a.build.absolute()/'bin'),
      '-DWITH_COMPILER_CCACHE=OFF','-DWITH_BUILDINFO=OFF','-DWITH_GTESTS=OFF',
      '-DWITH_CYCLES_DEVICE_CUDA=ON','-DWITH_CYCLES_CUDA_BINARIES='+cuda_kernels,
      '-DCYCLES_CUDA_BINARIES_ARCH='+a.cuda_architectures,'-DWITH_CYCLES_HIP_BINARIES=OFF',
      '-DWITH_CYCLES_ONEAPI_BINARIES=OFF','-DWITH_CYCLES_DEVICE_OPTIX='+optix_device,
      '-DWITH_CYCLES_DEVICE_HIP=OFF','-DWITH_CYCLES_DEVICE_ONEAPI=OFF',
      '-DWITH_VULKAN_BACKEND=OFF','-DWITH_BLENDER_THUMBNAILER=OFF']
args+=compiler_args()
if a.cuda:args+=['-DCUDA_TOOLKIT_ROOT_DIR='+str(a.cuda.absolute())]
if a.optix:args+=['-DOPTIX_ROOT_DIR='+str(a.optix.absolute())]
if a.toolchain:args+=['-DCMAKE_TOOLCHAIN_FILE='+str(a.toolchain.absolute())]
if platform.system()=='Windows':args+=['-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL']
else:
    for lang in ['C','CXX']:
        args+=['-DCMAKE_'+lang+'_FLAGS=-ffile-prefix-map='+str(root.parent)+'=.']
subprocess.run(args,check=True)
targets=['bts-host','blender']+install_only_targets(a.build.absolute())
print('Building: '+' '.join(targets),flush=True)
try:
    subprocess.run(['cmake','--build',str(a.build.absolute()),'--parallel',str(a.jobs),'--target',*targets],check=True)
except subprocess.CalledProcessError:
    report_unparsable_ninja(a.build.absolute());raise
subprocess.run(['cmake','--install',str(a.build.absolute()),'--config','Release'],check=True)
