#!/usr/bin/env python3
"""Configure and build the native host with upstream precompiled libraries."""
import argparse, os, pathlib, platform, subprocess
p=argparse.ArgumentParser()
p.add_argument('--blender',required=True,type=pathlib.Path)
p.add_argument('--build',required=True,type=pathlib.Path)
p.add_argument('--qt',required=True,type=pathlib.Path)
p.add_argument('--jobs',default=4,type=int)
p.add_argument('--toolchain',type=pathlib.Path)
a=p.parse_args();root=pathlib.Path(__file__).absolute().parents[1]
if platform.system()=='Darwin':
    raise SystemExit('macOS viewport unavailable: this host requires OpenGL 4.3; Blender 5.2 uses Metal on macOS. A Metal/Qt presentation port is required before packaging.')
args=['cmake','-S',str(a.blender.absolute()),'-B',str(a.build.absolute()),'-G','Ninja',
      '-DCMAKE_BUILD_TYPE=Release','-DCMAKE_PREFIX_PATH='+str(a.qt.absolute()),
      '-DBTS_HOST_SOURCE_DIR='+str(root/'native/host'),
      '-DCMAKE_INSTALL_PREFIX='+str(a.build.absolute()/'bin'),
      '-DWITH_COMPILER_CCACHE=OFF','-DWITH_BUILDINFO=OFF','-DWITH_GTESTS=OFF',
      '-DWITH_CYCLES_CUDA_BINARIES=OFF','-DWITH_CYCLES_HIP_BINARIES=OFF',
      '-DWITH_CYCLES_ONEAPI_BINARIES=OFF','-DWITH_CYCLES_DEVICE_OPTIX=OFF',
      '-DWITH_CYCLES_DEVICE_HIP=OFF','-DWITH_CYCLES_DEVICE_ONEAPI=OFF',
      '-DWITH_VULKAN_BACKEND=OFF','-DWITH_BLENDER_THUMBNAILER=OFF']
if a.toolchain:args+=['-DCMAKE_TOOLCHAIN_FILE='+str(a.toolchain.absolute())]
if platform.system()=='Windows':args+=['-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL']
else:
    for lang in ['C','CXX']:
        args+=['-DCMAKE_'+lang+'_FLAGS=-ffile-prefix-map='+str(root.parent)+'=.']
subprocess.run(args,check=True)
subprocess.run(['cmake','--build',str(a.build.absolute()),'--parallel',str(a.jobs),'--target','bts-host','blender'],check=True)
subprocess.run(['cmake','--install',str(a.build.absolute()),'--config','Release'],check=True)
