# SPDX-License-Identifier: GPL-3.0-or-later
"""Detached native image replacement proof using coherent frozen native inputs.

Run in the build container at nice 19. No GPU context is created by the CPU
suite. The snapshot must match the worktree's native ABI. Only generated private
fixtures and caches are written; source libraries are never edited.
"""
import argparse
import json
import os
from pathlib import Path
import shlex
import subprocess
from concurrent.futures import ThreadPoolExecutor

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--host-snapshot',required=True,type=Path)
p.add_argument('--runtime-template',required=True,type=Path,help='Immutable directory containing installed 5.2 scripts')
p.add_argument('--output',required=True,type=Path)
p.add_argument('--suite',choices=['cpu','gpu','jobs'],default='cpu')
p.add_argument('--libraries',type=Path,help='Optional private six-source JSON manifest; CPU inventory only')
p.add_argument('--runtime-baseline',action='store_true')
p.add_argument('--sanitize',action='store_true')
p.add_argument('--compile-only',action='store_true')
p.add_argument('--run-only',action='store_true')
p.add_argument('--lsan-suppressions',type=Path)
a=p.parse_args()
root=Path(__file__).resolve().parents[2]
snapshot=a.host_snapshot.resolve();output=a.output.resolve()
if not output.is_relative_to(root.parent/'build') or output==snapshot:raise RuntimeError('Use private workspace build output')
output.mkdir(parents=True,exist_ok=True)
source_head=json.loads((snapshot/'snapshot.json').read_text())['source_head']
protected=['NativeMaterialParameters.hh','LayerEffect.hh','MaskEffect.hh','FillProjection.hh','NativeMaterialGpuProgram.hh','NativeMaterialGraph.hh','AssetCatalog.hh']
for name in protected:
 old=subprocess.check_output(['git','show',source_head+':native/host/'+name],cwd=root)
 if old!=(root/'native/host'/name).read_bytes():raise RuntimeError('Frozen native ABI mismatch: '+name+'; use a coherent snapshot/rebuild')
(output/'objects').mkdir(exist_ok=True)
commands=json.loads((snapshot/'host-commands.json').read_text())
base=shlex.split(next(c for c in commands if '/main.cc.o' in c and ' -c ' in c))
oldmain=base[base.index('-o')+1]
sources=['native/host/'+s+'.cc' for s in ['NativeMaterialImageBindings','NativeMaterialImageVariant','NativeMaterialSourceFiles','NativeMaterialResource','NativeMaterialArchive','BlendMaterial','AssetCatalog','ResourceIO']]
if a.suite=='jobs':sources+=['native/host/NativeMaterialImageJobs.cc','native/host/NativeMaterialImagePanel.cc']
sources+=['native/tests/native_material_image_variant'+('_'+a.suite if a.suite!='cpu' else '')+'_test.cc']
if a.sanitize and a.suite=='gpu':raise RuntimeError('Use CPU suite for sanitizers')
if a.runtime_baseline and (a.libraries or a.suite!='cpu'):raise RuntimeError('Runtime baseline is separate from fixture/GPU tests')
flags=['-fsanitize=address,undefined','-fno-omit-frame-pointer'] if a.sanitize else []
objects=[];compiles=[]
for source in sources:
 obj=str(output/'objects'/('bts-variant__'+source.replace('/','__')+'.o'));objects.append(obj);command=base.copy()
 command[command.index('-o')+1]=obj;command[command.index('-c')+1]=str(root/source)
 for opt,val in [('-MF',obj+'.d'),('-MT',obj)]:
  if opt in command:command[command.index(opt)+1]=val
 command[2:2]=['-I'+str(root/'native/host')]
 command+=['-O1','-g0']+flags;compiles.append(command)
link=shlex.split(commands[-1])
if link[:2]==[':','&&']:link=link[2:]
if '&&' in link:link=link[:link.index('&&')]
link=[x for x in link if x!=oldmain and not any(x.endswith('/'+Path(source).name+'.o') for source in sources)]
for i,x in enumerate(link):
 if x.startswith('bin/lib/') or x.endswith(('.o','.a')) and not Path(x).is_absolute():
  private=snapshot/x
  if not private.is_file():raise RuntimeError('Missing immutable input '+str(private))
  link[i]=str(private)
 elif x.endswith(('.o','.a')) and Path(x).is_absolute() and not Path(x).resolve().is_relative_to((root.parent/'blender/lib').resolve()):
  raise RuntimeError('Unexpected mutable absolute link input '+x)
executable=output/'native-image-variant-test'
link[1:1]=objects;link[link.index('-o')+1]=str(executable)
link=['-Wl,--dependency-file='+str(output/'link.d') if x.startswith('-Wl,--dependency-file=') else x for x in link]+flags
sdk=root.parent/'qt/6.8.3'
env=dict(os.environ,QT_QPA_PLATFORM='offscreen',QT_PLUGIN_PATH=str(sdk/'plugins'),
 OCIO=str(root.parent/'blender/release/datafiles/colormanagement/config.ocio'),BTS_ROOT=str(root.parent),BTS_HEADLESS_WORKSPACE='1',
 LD_LIBRARY_PATH=str(sdk/'lib')+':'+str(snapshot/'bin/lib')+':'+os.environ.get('LD_LIBRARY_PATH',''))
for var,folder in [('XDG_CONFIG_HOME','config'),('XDG_DATA_HOME','data'),('XDG_CACHE_HOME','cache'),('BTS_RESOURCE_CACHE','resources'),('BTS_RECOVERY_DIR','recovery'),('TMPDIR','tmp')]:
 (output/folder).mkdir(exist_ok=True);env[var]=str(output/folder)
if a.sanitize:env.update(ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1')
if a.lsan_suppressions:env['LSAN_OPTIONS']='suppressions='+str(a.lsan_suppressions.resolve())+':print_suppressions=1'
if not a.compile_only and not (output/'5.2').exists():subprocess.run(['cp','-a','--reflink=auto',str(a.runtime_template.resolve()/'5.2'),str(output/'5.2')],check=True)
def compile_one(command):
 source=Path(command[command.index('-c')+1]);print('Compiling',source.name,flush=True)
 with (output/(source.stem+'.compile.log')).open('w') as log:
  result=subprocess.run(command,cwd=snapshot,env=env,stdout=log,stderr=subprocess.STDOUT,timeout=300)
 if result.returncode:raise RuntimeError('Compile failed: '+str(output/(source.stem+'.compile.log')))
if not a.run_only:
 with ThreadPoolExecutor(max_workers=2) as pool:
  for _ in pool.map(compile_one,compiles):pass
run=[str(executable),str(output/'fixtures')]+(['--runtime-baseline'] if a.runtime_baseline else [str(a.libraries.resolve())] if a.libraries and a.suite=='cpu' else [])
if a.suite=='gpu':run=['bash','-c','source "$1"; shift; exec "$@"','native-image-variant',str(root/'scripts/host/gpu-env.sh'),*run]
with (output/'run.log').open('w') as log:
 for command in ([] if a.run_only else [link])+([] if a.compile_only else [run]):
  print('Linking' if command==link else 'Running detached '+a.suite+' fixture',flush=True)
  result=subprocess.run(command,cwd=snapshot,env=env,stdout=log,stderr=subprocess.STDOUT,timeout=300)
  if result.returncode:raise RuntimeError('Command failed: '+str(output/'run.log'))
metadata_object=str(output/'variant-metadata.o');metadata=base.copy()
metadata[metadata.index('-o')+1]=metadata_object;metadata[metadata.index('-c')+1]=str(root/'native/tests/native_material_image_variant_metadata_test.cc')
for opt,val in [('-MF',metadata_object+'.d'),('-MT',metadata_object)]:
 if opt in metadata:metadata[metadata.index(opt)+1]=val
metadata[2:2]=['-I'+str(root/'native/host')];metadata+=['-O1','-g0']+flags
metadata_binary=str(output/'variant-metadata-test')
if not a.run_only:
 compile_one(metadata)
 with (output/'metadata-link.log').open('w') as log:
  subprocess.run(['/usr/bin/c++',metadata_object,'-o',metadata_binary,'-L'+str(sdk/'lib'),'-lQt6Core','-Wl,-rpath,'+str(sdk/'lib'),'-pthread']+flags,env=env,stdout=log,stderr=subprocess.STDOUT,check=True)
if not a.compile_only:
 with (output/'metadata.log').open('w') as log:subprocess.run([metadata_binary],env=env,stdout=log,stderr=subprocess.STDOUT,check=True)
print('Native image variant proof PASS')
