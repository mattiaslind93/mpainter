"""Exact old/new LayerEditDiff through the production artist callback/frame path.

Compile one production-main driver, then link separate tiny old/new diff bridges.
A unique implementation symbol prevents weak inline ODR selection from other
host objects. Only immutable copied/reflinked host link inputs are accepted.
Coordinate GPU contexts before --run-only; all application paths are private.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shlex
import subprocess

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--host-snapshot',type=Path,required=True)
p.add_argument('--host-resources',type=Path,required=True)
p.add_argument('--output',type=Path,required=True)
p.add_argument('--baseline-ref',default='ba0b2b2')
p.add_argument('--build-only',action='store_true')
p.add_argument('--run-only',action='store_true')
p.add_argument('--cpu-only',action='store_true')
a=p.parse_args();repo=Path(__file__).resolve().parents[2];bts=Path(os.environ.get('BTS_ROOT',repo.parent)).resolve();snapshot=a.host_snapshot.resolve();out=a.output.resolve()
if not out.is_relative_to(bts/'build') or out in (bts/'build',snapshot) or out.is_relative_to(snapshot) or out.is_relative_to(bts/'build/host'):
 raise SystemExit('Output must be private build storage outside host/snapshot')

if os.name=='posix':
 import resource
 resource.setrlimit(resource.RLIMIT_CORE,(0,0))
out.mkdir(parents=True,exist_ok=True);(out/'bin').mkdir(exist_ok=True);(out/'tmp').mkdir(exist_ok=True)
env=dict(os.environ,CCACHE_DIR=str(bts/'build/ccache'),TMPDIR=str(out/'tmp'))
if not a.run_only:
 commands=json.loads((snapshot/'host-commands.json').read_text())
 def compilation(name):return shlex.split(next(c for c in commands if ' -o ' in c and '/'+name+'.o -c ' in c))
 baseline=out/'baseline';baseline.mkdir(exist_ok=True)
 source_bts=Path(compilation('main.cc')[compilation('main.cc').index('-c')+1]).parents[3]
 original=subprocess.check_output(['git','show',a.baseline_ref+':native/host/LayerEditDiff.hh'],cwd=repo)
 (baseline/'LayerEditDiff.hh').write_bytes(original)
 def compile(name,label,old=False):
  args=compilation('main.cc' if name=='native_material_artist_edit_test.cc' else 'MeshBaker.cc')
  previous=Path(args[args.index('-c')+1]).parent
  args=[arg.replace(str(previous),str(repo/'native/host')) for arg in args]
  args=['-O0' if arg in ('-O1','-O2','-O3') else arg for arg in args]
  obj=out/(label+'.o');args[args.index('-o')+1]=str(obj);args[args.index('-c')+1]=str(repo/'native/tests'/name)
  for option,value in [('-MF',str(obj)+'.d'),('-MT',str(obj))]:
   if option in args:args[args.index(option)+1]=value
  args.extend(['-UNDEBUG','-g0'])
  # Put directories after the compiler/ccache executables, before inherited
  # include flags. The old header must win for both tiny baseline TUs.
  first_option=next(i for i,arg in enumerate(args)if arg.startswith('-'))
  dirs=([baseline]if old else[])+[repo/'native/host']
  args[first_option:first_option]=['-I'+str(path)for path in dirs]
  print('Compile '+label,flush=True)
  with(out/(label+'.compile.log')).open('w')as log:subprocess.run(['nice','-n','19',*args],cwd=snapshot,env=env,stdout=log,stderr=subprocess.STDOUT,check=True)
  return obj
 objects={}
 for variant in ('baseline','fixed'):
  objects[variant+'-cpu']=compile('native_material_edit_diff_test.cc',variant+'-cpu',variant=='baseline')
  if not a.cpu_only:objects[variant+'-bridge']=compile('native_material_artist_edit_diff.cc',variant+'-bridge',variant=='baseline')
 if not a.cpu_only:
  objects['artist']=compile('native_material_artist_edit_test.cc','artist')
  symbols=subprocess.check_output(['nm','-C',str(objects['artist'])],text=True)
  assert ' U bts::native_artist_edit_diff('in symbols
  assert not any(name in symbols for name in ('bts::layer_edit_channels(', 'bts::native_artist_unused_inline_diff('))

 link=shlex.split(commands[-1])
 if link[:2]==[':','&&']:link=link[2:]
 if '&&'in link:link=link[:link.index('&&')]
 link=[arg for arg in link if not arg.endswith('/main.cc.o')]
 for arg in link:
  if arg.endswith(('.o','.a')):
   path=Path(arg);path=(path if path.is_absolute()else snapshot/path).resolve()
   if not path.is_file()or path.is_relative_to((bts/'build/host').resolve()):raise SystemExit('Non-frozen/missing link input: '+str(path))
 for variant in ('baseline','fixed'):
  for target in (('cpu',)if a.cpu_only else('cpu','artist')):
   args=list(link);first=next(i for i,arg in enumerate(args)if arg.endswith('.o'))
   args[first:first]=[str(objects[variant+'-cpu'])]if target=='cpu'else[str(objects['artist']),str(objects[variant+'-bridge'])]
   args[args.index('-o')+1]=str(out/'bin'/(variant+'-'+target))
   args=['-Wl,--dependency-file='+str(out/(variant+'-'+target+'.link.d'))if arg.startswith('-Wl,--dependency-file=')else arg.replace(str(source_bts/'build/host/bin/lib'),str(snapshot/'bin/lib')).replace(str(bts/'build/host/bin/lib'),str(snapshot/'bin/lib'))for arg in args]
   print('Link '+variant+'-'+target,flush=True)
   with(out/(variant+'-'+target+'.link.log')).open('w')as log:subprocess.run(['nice','-n','19',*args],cwd=snapshot,env=env,stdout=log,stderr=subprocess.STDOUT,check=True)
 for resource in a.host_resources.resolve().iterdir():
  if resource.is_dir()and resource.name[0].isdigit()and not(out/'bin'/resource.name).exists():(out/'bin'/resource.name).symlink_to(resource,target_is_directory=True)
 (out/'build.json').write_text(json.dumps({'source':str(repo),'snapshot':str(snapshot),'baseline_ref':a.baseline_ref,'baseline_header_sha256':hashlib.sha256(original).hexdigest(),'objects':{k:str(v)for k,v in objects.items()}},indent=2)+'\n')
if not a.build_only:
 env.update(BTS_ROOT=str(bts),QT_QPA_PLATFORM='offscreen',BTS_HEADLESS_WORKSPACE='1',
  QT_PLUGIN_PATH=str(bts/'qt/6.8.3/plugins'),LD_LIBRARY_PATH=str(bts/'qt/6.8.3/lib')+':'+str(snapshot/'bin/lib')+':'+str(a.host_resources.resolve()/'lib')+(':'+env['LD_LIBRARY_PATH']if env.get('LD_LIBRARY_PATH')else ''))
 def private(directory):
  result=dict(env)
  for key,folder in [('XDG_CONFIG_HOME','config'),('XDG_DATA_HOME','data'),('XDG_CACHE_HOME','cache'),('XDG_STATE_HOME','state'),('TMPDIR','tmp'),('BTS_RECOVERY_DIR','recovery'),('BTS_SESSION_LOG_DIR','logs'),('BTS_RESOURCE_CACHE','resources')]:
   (directory/folder).mkdir(parents=True,exist_ok=True);result[key]=str(directory/folder)
  return result
 for variant in ('baseline','fixed'):
  with(out/(variant+'-cpu.log')).open('w')as log:result=subprocess.run(['nice','-n','19',str(out/'bin'/(variant+'-cpu'))],env=private(out/(variant+'-cpu-data')),stdout=log,stderr=subprocess.STDOUT,timeout=60)
  text=(out/(variant+'-cpu.log')).read_text()
  assert (result.returncode!=0 and 'all_supported' in text and 'Assertion' in text)if variant=='baseline'else(result.returncode==0),(variant,result.returncode,text[-2000:])
 if a.cpu_only:
  print('CPU baseline missing native invalidation reproduced; fixed diff cases PASS');raise SystemExit(0)
 fixtures=out/'fixtures';fixtures.mkdir(exist_ok=True)
 with(out/'fixtures.log').open('w')as log:subprocess.run(['nice','-n','19',str(bts/'build/baseline/bin/blender'),'--background','--factory-startup','--python',str(repo/'scripts/host/generate-blend-material-fixture.py'),'--',str(fixtures)],env=private(out/'fixture-data'),stdout=log,stderr=subprocess.STDOUT,check=True,timeout=90)
 source=fixtures/'materials.blend';source_sha=hashlib.sha256(source.read_bytes()).hexdigest()
 for variant in ('baseline','fixed'):
  directory=out/('run-'+variant);directory.mkdir(exist_ok=True);runtime=private(directory)
  runtime.update(BTS_HOST_EXECUTABLE=str(out/'bin'/(variant+'-artist')),BTS_BLEND_MATERIAL_DEMO='1',BTS_BLEND_MATERIAL_SOURCE=str(source),BTS_TEMPLATE_DIR=str(directory/'templates'),BTS_ENVIRONMENT=env.get('BTS_ENVIRONMENT',str(bts/'build/environment-cache/TT_environmentLights_v001.hdr')))
  print('Run actual artist callbacks '+variant,flush=True)
  with(directory/'run.log').open('w')as log:result=subprocess.run(['nice','-n','19','bash',str(repo/'scripts/host/run-host.sh'),'--no-splash','--workspace','--gui-fix-demo',str(directory),'--gui-fix-mesh',str(repo/'native/tests/fixtures/single_tile.obj'),'--gui-fix-resolution','128','--autosave-seconds','3600','--library-dir',str(directory/'library')],cwd=repo,env=runtime,stdout=log,stderr=subprocess.STDOUT,timeout=300)
  text=(directory/'run.log').read_text();assert'BTS_SHUTDOWN clean'in text and'Not freed memory blocks'not in text
  if variant=='baseline':assert result.returncode!=0 and'BTS_NATIVE_ARTIST_DIFF mask=0'in text and'Native artist edit GPU stale: stage=edited descriptor=0.73'in text,text[-3000:]
  else:
   assert result.returncode==0 and'BTS_NATIVE_ARTIST_EDIT PASS'in text,text[-3000:]
   assert not any(word in text for word in ('BTS_GPU_STACK_FALLBACK','BTS_GPU_AUTHORING_CPU_READ','BTS_DRAW_FAILED','BTS_HOST_FAILED','Shader compilation failed')),text[-3000:]
   proof=json.loads((directory/'native-artist-edit.json').read_text());assert len(proof['captures'])==4 and proof['one_gesture']and proof['zero_shader_recompiles']and proof['cpu_recipe_materializations']==0
  assert hashlib.sha256(source.read_bytes()).hexdigest()==source_sha
 proof['source_unchanged']=True;proof['source_sha256']=source_sha;proof['baseline_zero_invalidation_stale_gpu_reproduced']=True
 proof['baseline_ref']=a.baseline_ref
 proof['production_main_sha256']=hashlib.sha256((repo/'native/host/main.cc').read_bytes()).hexdigest()
 proof['fixed_diff_header_sha256']=hashlib.sha256((repo/'native/host/LayerEditDiff.hh').read_bytes()).hexdigest()
 proof['unique_exact_diff_implementation_symbol']=True

 (out/'verified.json').write_text(json.dumps(proof,indent=2)+'\n');print('Native artist edit: baseline stale GPU reproduced; fixed widget/frame/Undo/Redo PASS')
