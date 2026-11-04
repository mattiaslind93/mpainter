"""Build a coherent private native ABI against an immutable Blender snapshot.

Run inside the compiler container with nice -n19. No artist build inputs are used.
"""
import argparse
import hashlib
from concurrent.futures import ThreadPoolExecutor
import json
import os
from pathlib import Path
import shlex
import subprocess
import time

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--host-snapshot',type=Path,required=True)
p.add_argument('--output',type=Path,required=True)
p.add_argument('--compile-only',action='store_true')
p.add_argument('--link-only',action='store_true')
p.add_argument('--run-only',action='store_true',help='Run the CPU and offscreen GPU oracles after reserving a correctness GPU slot')
p.add_argument('--host-resources',type=Path,help='SDK bin directory containing the versioned resources')
p.add_argument('--resume-cases',type=Path,help='Resume completed oracle cases only when production and oracle hashes match')
p.add_argument('--unit',action='append',default=[],help='Recompile a changed source basename after an initial coherent build')
p.add_argument('--header',action='append',default=[],help='Refresh the complete include closure from private compiler dependency files')
a=p.parse_args()
repo=Path(os.environ.get('BTS_REPO',Path(__file__).resolve().parents[2])).resolve()
snap=a.host_snapshot.resolve();out=a.output.resolve()
if not out.is_relative_to(repo.parent/'build') or out.is_relative_to(snap) or out.is_relative_to(repo.parent/'build/host'):
 raise SystemExit('Use separate private build output')
out.mkdir(parents=True,exist_ok=True)
commands=json.loads((snap/'host-commands.json').read_text())
template=shlex.split(next(c for c in commands if '/main.cc.o -c ' in c))
old=Path(template[template.index('-c')+1]).parents[2]
env=dict(os.environ,CCACHE_DIR=str(repo.parent/'build/ccache'))
if a.run_only:
 production=hashlib.sha256()
 for key,value in sorted(json.loads((out/'objects.json').read_text()).items()):
  if key.endswith('.o') and '/__extra__/' not in key and not key.startswith('__extra__/'):
   production.update(key.encode());production.update(hashlib.sha256(Path(value).read_bytes()).digest())
 production.update(hashlib.sha256((out/'objects/__extra__/LinearColorTransform.cc.o').read_bytes()).digest())
 oracle=(repo/'native/tests/ordered_raw_rgb_gpu_test.cc').read_text().replace('cache.displacement_summaries();','')
 oracle_sha=hashlib.sha256((oracle+(repo/'native/tests/OrderedRawRgbFixture.hh').read_text()).encode()).hexdigest()
 env.update(BTS_ORDERED_PRODUCTION_SHA=production.hexdigest(),BTS_ORDERED_ORACLE_SHA=oracle_sha)
 if a.resume_cases:
  resume=json.loads(a.resume_cases.read_text())
  if resume.get('production_sha')!=production.hexdigest() or resume.get('oracle_sha')!=oracle_sha:raise SystemExit('Resume source/oracle hashes differ; no GPU process started')
  env['BTS_ORDERED_RESUME_CASES']=str(a.resume_cases.resolve())
 resources=(a.host_resources or snap/'bin').resolve()
 for resource in resources.iterdir():
  if resource.is_dir() and resource.name[0].isdigit() and not (out/resource.name).exists():(out/resource.name).symlink_to(resource,target_is_directory=True)
 run=out/('run-'+time.strftime('%Y%m%d-%H%M%S'));run.mkdir()
 (out/'latest-run.txt').write_text(str(run)+'\n')
 env.update(LD_LIBRARY_PATH=str(snap/'bin/lib')+':'+env.get('LD_LIBRARY_PATH',''))
 with (run/'cpu.log').open('w') as log:subprocess.run(['nice','-n','19',str(out/'cpu-test')],cwd=repo,env=env,stdout=log,stderr=subprocess.STDOUT,check=True)
 wrapper=out/'gpu-wrapper'
 wrapper.write_text('#!/usr/bin/env python3\nimport os,sys\nos.execv('+repr(str(out/'gpu-test'))+',["gpu-test",*sys.argv[2:]])\n');wrapper.chmod(0o755)
 for cache in ['nvidia-cache','mesa-cache']:(run/cache).mkdir()
 env.update(__GL_SHADER_DISK_CACHE='1',BTS_ROOT=str(repo.parent),BTS_HOST_EXECUTABLE=str(wrapper),QT_QPA_PLATFORM='offscreen',BTS_HEADLESS_WORKSPACE='1',
  XDG_CONFIG_HOME=str(run/'config'),XDG_DATA_HOME=str(run/'data'),XDG_STATE_HOME=str(run/'state'),XDG_CACHE_HOME=str(run/'cache'),
  BTS_ENVIRONMENT=str(repo.parent/'build/environment-cache/TT_environmentLights_v001.hdr'),BTS_RECOVERY_DIR=str(run/'recovery'),BTS_RESOURCE_CACHE=str(run/'resources'),BTS_SESSION_LOG_DIR=str(run/'logs'),
  __GL_SHADER_DISK_CACHE_PATH=str(run/'nvidia-cache'),MESA_SHADER_CACHE_DIR=str(run/'mesa-cache'))
 for label,extra in [('run',[]),('cold-open',['--cold-open'])]:
  if extra:
   env.pop('BTS_ORDERED_RESUME_CASES',None)
   for key,directory in [('XDG_CONFIG_HOME','config'),('XDG_DATA_HOME','data'),('XDG_STATE_HOME','state'),('XDG_CACHE_HOME','cache'),('BTS_RECOVERY_DIR','recovery'),('BTS_RESOURCE_CACHE','resources')]:env[key]=str(run/(directory+'-cold'))
  print('Native GPU phase',label,'evidence',run/(label+'.log'),flush=True)
  with (run/(label+'.log')).open('w') as log:result=subprocess.run(['nice','-n','19','bash',str(repo/'scripts/host/run-host.sh'),'--no-splash',str(run),*extra],cwd=repo,env=env,stdout=log,stderr=subprocess.STDOUT,timeout=600)
  for _ in range(100):
   text=(run/(label+'.log')).read_text()
   if 'BTS_SHUTDOWN clean' in text:break
   time.sleep(.02)
  if result.returncode or 'BTS_SHUTDOWN clean' not in text or 'Not freed memory blocks' in text:raise SystemExit('Ordered RGB failed: '+text[-5000:])
  (run/('case-checkpoint-'+label+'.json')).write_bytes((run/'case-checkpoint.json').read_bytes())
 proof=json.loads((run/'verified.json').read_text());cold=json.loads((run/'cold-open.json').read_text())
 for key in ['maximum_literal_quantized_delta','maximum_float32_analytic_error']:proof[key]=max(proof[key],cold[key])
 proof['rounding_boundary_components']+=cold['rounding_boundary_components']
 assert cold['source_free_cold_exr_save_open'] and cold['fresh_process'] and cold['shared_raw_cpu_gpu_resource'] and cold['exact_signed_hdr_source_rgb']
 proof['production_sha']=env['BTS_ORDERED_PRODUCTION_SHA'];proof['oracle_sha']=oracle_sha;proof['executable_sha']=hashlib.sha256((out/'gpu-test').read_bytes()).hexdigest()
 proof['source_free_save_open']=True;proof['source_free_cold_exr_save_open']=True;proof['cases']+=cold['cases'];proof['components']+=cold['components'];proof['maximum_error']=max(proof['maximum_error'],cold['maximum_error'])
 assert proof['maximum_error']<3e-5 and proof['interactive_cpu_composition'] is False
 (run/'verified.json').write_text(json.dumps(proof,indent=2)+'\n');(out/'verified.json').write_text(json.dumps(proof,indent=2)+'\n')
 print((run/'cpu.log').read_text());print(json.dumps(proof,indent=2));raise SystemExit(0)
units=[]
for command in commands:
 args=shlex.split(command)
 if '-c' in args and Path(args[args.index('-c')+1]).is_relative_to(old):units.append(args)
for source in ['native/host/LinearColorTransform.cc','native/tests/linear_color_transform_test.cc','native/tests/ordered_raw_rgb_gpu_test.cc']:
 args=template.copy();args[args.index('-c')+1]=str(old/source);args[args.index('-o')+1]='__extra__/'+Path(source).name+'.o';units.append(args)
def compile(args):
 previous=args[args.index('-o')+1];source=Path(args[args.index('-c')+1]);obj=out/'objects'/previous
 obj.parent.mkdir(parents=True,exist_ok=True)
 args=[x.replace(str(old),str(repo)) for x in args]
 args[args.index('-o')+1]=str(obj);args[args.index('-MF')+1]=str(obj)+'.d'
 if source.parent.name=='tests':args.append('-UNDEBUG')
 print('Compile',source.name,flush=True)
 with Path(str(obj)+'.log').open('w') as log:subprocess.run(['nice','-n','19',*args],cwd=snap,env=env,stdout=log,stderr=subprocess.STDOUT,check=True)
 return previous,str(obj)
if not a.link_only:
 def affected(args):
  if not a.unit and not a.header:return True
  if Path(args[args.index('-c')+1]).name in a.unit:return True
  if not a.header:return False
  dep=out/'objects'/(args[args.index('-o')+1]+'.d')
  if not dep.is_file():raise SystemExit('Incomplete native dependency coverage: '+str(dep))
  text=dep.read_text();return any('/'+header in text for header in a.header)
 selected=[args for args in units if affected(args)]
 if not set(a.unit).issubset({Path(args[args.index('-c')+1]).name for args in selected}):raise SystemExit('Unknown native source requested')
 if not selected:raise SystemExit('No matching native source/include consumers')
 (out/'compile-selection.json').write_text(json.dumps([{'source':args[args.index('-c')+1],'target_object':args[args.index('-o')+1]} for args in selected],indent=2)+'\n')
 replacements=json.loads((out/'objects.json').read_text()) if a.unit or a.header else {}
 with ThreadPoolExecutor(max_workers=2) as pool:replacements.update(pool.map(compile,selected))
 (out/'objects.json').write_text(json.dumps(replacements,indent=2)+'\n')
else:replacements=json.loads((out/'objects.json').read_text())
if a.compile_only:raise SystemExit(0)
native_archives=[];archive_manifest={}
for command in commands:
 if 'ar qc' not in command or 'libbts-' not in command:continue
 ar=shlex.split(next(part for part in command.split(' && ') if 'ar qc' in part))
 archive=ar[2];target=out/'archives'/Path(archive).name;target.parent.mkdir(exist_ok=True)
 members=[replacements.get(x,x) for x in ar[3:]]
 if any(member not in replacements.values() for member in members):raise SystemExit('Native archive retains an unrebuilt member: '+archive)
 target.unlink(missing_ok=True)
 subprocess.run(['nice','-n','19',ar[0],'qc',str(target),*members],cwd=snap,check=True)
 subprocess.run(['nice','-n','19','ranlib',str(target)],check=True)
 inventory=[]
 for member in members:
  digest=hashlib.sha256(Path(member).read_bytes()).hexdigest()
  packed=subprocess.check_output(['nice','-n','19',ar[0],'p',str(target),Path(member).name])
  if hashlib.sha256(packed).hexdigest()!=digest:raise SystemExit('Native archive member does not match its fresh target-qualified object')
  inventory.append({'object':member,'sha256':digest})
 archive_manifest[str(target)]=inventory
 replacements[archive]=str(target)
 native_archives.append(archive)
(out/'native-archives.json').write_text(json.dumps(archive_manifest,indent=2)+'\n')
link_manifest={}
for label,test in [('bts-host',None),('cpu-test','linear_color_transform_test.cc'),('gpu-test','ordered_raw_rgb_gpu_test.cc')]:
 link=shlex.split(commands[-1].split(' && ')[1]);main=next(key for key in replacements if key.endswith('/main.cc.o'))
 selected=dict(replacements)
 if test:selected[main]=selected['__extra__/'+test+'.o']
 link=[selected.get(x,x) for x in link]
 first=next(i for i,x in enumerate(link) if x.endswith('.o'))
 link.insert(first,replacements['__extra__/LinearColorTransform.cc.o'])
 link[link.index('-o')+1]=str(out/label)
 link=[('-Wl,--dependency-file='+str(out/(label+'.d'))) if x.startswith('-Wl,--dependency-file=') else x.replace(str(repo.parent/'build/host/bin/lib'),str(snap/'bin/lib')) for x in link]
 if label=='cpu-test':
  # Match the canonical Qt-only target, without letting unrelated native host
  # objects hide a missing test dependency.
  model=lambda name:next(value for key,value in replacements.items() if key.endswith('/'+name+'.o') and 'bts-host.dir' in key)
  qt=repo.parent/'qt/6.8.3/lib'
  compiler=template[:2] if Path(template[0]).name=='ccache' else template[:1]
  link=[*compiler,'-o',str(out/label),replacements['__extra__/'+test+'.o'],model('LayerApi.cc'),model('SmartMaterial.cc'),
   replacements['lib/libbts-export-config.a'],'-L'+str(qt),'-Wl,-rpath,'+str(qt),'-lQt6Core','-lz']
 for x in link:
  if x.endswith(('.o','.a')):
   path=Path(x);path=path if path.is_absolute() else snap/path
   if not path.is_file() or path.is_relative_to(repo.parent/'build/host'):raise SystemExit('Missing/mutable link input '+str(path))
 if any(x in replacements for x in link):raise SystemExit('Final link retained an old native object or archive')
 link_manifest[label]={'native_objects':[x for x in link if x.endswith('.o') and Path(x).is_relative_to(out/'objects')],
  'native_archives':[replacements[x] for x in native_archives if replacements[x] in link],
  'all_inputs':[x for x in link if x.endswith(('.o','.a'))]}
 (out/'link-inputs.json').write_text(json.dumps(link_manifest,indent=2)+'\n')
 print('Link',label,flush=True)
 with (out/(label+'.link.log')).open('w') as log:subprocess.run(['nice','-n','19',*link],cwd=snap,stdout=log,stderr=subprocess.STDOUT,check=True)
for resource in (snap/'bin').iterdir():
 if resource.is_dir() and resource.name[0].isdigit() and not (out/resource.name).exists():(out/resource.name).symlink_to(resource,target_is_directory=True)
