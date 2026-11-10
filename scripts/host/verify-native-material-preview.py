# SPDX-License-Identifier: GPL-3.0-or-later
"""Build and verify explicit native UV Base Color browser previews.

Run inside the compiler container. Supply a frozen host link snapshot, SDK data,
and an explicit read-only cases manifest. Coordinate a GPU slot before --run-only.
"""
from pathlib import Path
from concurrent.futures import ThreadPoolExecutor
import argparse
import json
import os
import shlex
import subprocess
import time

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--host-snapshot',type=Path,required=True)
p.add_argument('--host-resources',type=Path,required=True)
p.add_argument('--output',type=Path,required=True)
p.add_argument('--cases',type=Path,required=True)
p.add_argument('--build-only',action='store_true')
p.add_argument('--run-only',action='store_true')
a=p.parse_args()
repo=Path(__file__).resolve().parents[2]
bts=Path(os.environ.get('BTS_ROOT',repo.parent)).resolve()
import sys
sys.path.insert(0,str(repo/'scripts/host'))
from main_source import host_file  # host files live in domain folders
snap,out=a.host_snapshot.resolve(),a.output.resolve()
if out==bts/'build' or not out.is_relative_to(bts/'build') or out==snap or out.is_relative_to(snap) or out.is_relative_to(bts/'build/host'):
 raise SystemExit('Use private build output outside the immutable snapshot and artist build')
out.mkdir(parents=True,exist_ok=True)
cmds=json.loads((snap/'host-commands.json').read_text())
env=dict(os.environ,CCACHE_DIR=str(bts/'build/ccache'))
def compile(name):
 extra=name in ('NativeMaterialPreview.cc','NativeMaterialPreviewImages.cc','NativeMaterialDecodedImage.cc');test=name.endswith('_test.cc');template='BlenderHost.cc'if extra else 'main.cc'if test else name
 # The snapshot's native/host root (template sources may sit in domain folders).
 args=shlex.split(next(c for c in cmds if ' -o 'in c and '/'+template+'.o -c 'in c));old=args[args.index('-o')+1];src=args[args.index('-c')+1];root=src[:src.index('/native/host')+len('/native/host')];args=[x.replace(root,str(repo/'native/host'))for x in args];obj=out/(name+'.o');args[args.index('-o')+1]=str(obj);args[args.index('-MF')+1]=str(obj)+'.d';args[args.index('-c')+1]=str(repo/'native/tests'/name if test else host_file(repo,name));args.extend(['-UNDEBUG']if test else []);args=['-O1'if name=='main.cc'and x=='-O2'else x for x in args];print('Compile',name,flush=True)
 with(out/(name+'.compile.log')).open('w')as log:subprocess.run(['nice','-n','19',*args],cwd=snap,env=env,stdout=log,stderr=subprocess.STDOUT,check=True)
 return ('__extra__'+name if extra else '__test__'if test else old,str(obj))
if not a.run_only:
 units=['NativeMaterialDecodedImage.cc','NativeMaterialPreview.cc','NativeMaterialPreviewImages.cc','BlendAssetBrowser.cc','BlenderHost.cc','main.cc','native_material_preview_test.cc']
 with ThreadPoolExecutor(max_workers=2) as pool:
  repl=dict(pool.map(compile,units))
 (out/'compiled.json').write_text(json.dumps(repl,indent=2)+'\n')
 for label,test in [('preview-test',True),('bts-host',False)]:
  link=shlex.split(cmds[-1].split(' && ')[1]);link=[repl.get(x,x)for x in link]
  if test:
   main=next(value for key,value in repl.items()if key.endswith('/main.cc.o'))
   link=[x for x in link if x!=main]
   link.insert(next(i for i,x in enumerate(link)if x.endswith('.o')),repl['__test__'])
  link[next(i for i,x in enumerate(link)if x.endswith('.o')):next(i for i,x in enumerate(link)if x.endswith('.o'))]=[value for key,value in repl.items()if key.startswith('__extra__')]
  link[link.index('-o')+1]=str(out/label)
  link=[('-Wl,--dependency-file='+str(out/(label+'.link.d')))if x.startswith('-Wl,--dependency-file=')else x.replace(str(bts/'build/host/bin/lib'),str(snap/'bin/lib'))for x in link]
  for x in link:
   if x.endswith(('.o','.a')):
    file=Path(x);file=file if file.is_absolute()else snap/file
    if not file.is_file()or file.is_relative_to(bts/'build/host'):
     raise SystemExit('Mutable/missing host input: '+str(file))
  print('Link',label,flush=True)
  with(out/(label+'.link.log')).open('w')as log:
   subprocess.run(['nice','-n','19',*link],cwd=snap,env=env,stdout=log,stderr=subprocess.STDOUT,check=True)
for resource in a.host_resources.resolve().iterdir():
 if resource.is_dir()and resource.name[0].isdigit()and not(out/resource.name).exists():
  (out/resource.name).symlink_to(resource,target_is_directory=True)
if not a.build_only:
 run=out/'run';run.mkdir(exist_ok=True)
 # The common launcher receives --no-splash. This focused test has its own main.
 wrapper=out/'preview-wrapper'
 wrapper.write_text('#!/usr/bin/env python3\nimport os,sys\nos.execv('+repr(str(out/'preview-test'))+',["preview-test",*sys.argv[2:]])\n')
 wrapper.chmod(0o755)
 env.update(BTS_ROOT=str(bts),BTS_HOST_EXECUTABLE=str(wrapper),QT_QPA_PLATFORM='offscreen',BTS_HEADLESS_WORKSPACE='1',
  XDG_CONFIG_HOME=str(run/'config'),XDG_DATA_HOME=str(run/'data'),XDG_STATE_HOME=str(run/'state'),XDG_CACHE_HOME=str(run/'cache'),
  BTS_ENVIRONMENT=str(bts/'build/environment-cache/TT_environmentLights_v001.hdr'),__GL_SHADER_DISK_CACHE_PATH=str(run/'nvidia-cache'),MESA_SHADER_CACHE_DIR=str(run/'mesa-cache'),BTS_RECOVERY_DIR=str(run/'recovery'),BTS_RESOURCE_CACHE=str(run/'resources'),BTS_SESSION_LOG_DIR=str(run/'logs'),
  LD_LIBRARY_PATH=str(snap/'bin/lib')+':'+env.get('LD_LIBRARY_PATH',''))
 with(run/'run.log').open('w')as log:
  result=subprocess.run(['nice','-n','19','bash',str(repo/'scripts/host/run-host.sh'),'--no-splash',str(run),str(a.cases.resolve())],cwd=repo,env=env,stdout=log,stderr=subprocess.STDOUT,timeout=240)
 for _ in range(100):
  text=(run/'run.log').read_text()
  if 'BTS_SHUTDOWN clean'in text or(result.returncode and text):break
  time.sleep(.02)
 if result.returncode or 'Not freed memory blocks'in text or 'BTS_SHUTDOWN clean'not in text:
  raise SystemExit('Native preview failed: '+text[-5000:])
 proof=json.loads((run/'verified.json').read_text())
 assert proof['cpu_native_composition']is False and proof['browse_prepare_calls']==0 and proof['shelf_installs']==0
 assert proof['maximum_error']<2e-6 and proof['display_maximum_byte_error']<=1
 print(json.dumps(proof,indent=2))
