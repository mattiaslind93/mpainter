"""Phase-one authored FLOAT Height proof; no UI or persistent policy activation.

Run inside the compiler container with an immutable, matching host snapshot.
Coordinate build/GPU slots first. Inputs are explicit read-only user .blend paths.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shlex
import subprocess
import time
import uuid

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--host-snapshot',type=Path,required=True)
p.add_argument('--host-resources',type=Path,required=True)
p.add_argument('--output',type=Path,required=True)
p.add_argument('--cases',type=Path,required=True)
p.add_argument('--build-only',action='store_true')
p.add_argument('--run-only',action='store_true')
p.add_argument('--cpu-only',action='store_true')
a=p.parse_args();repo=Path(__file__).resolve().parents[2];bts=Path(os.environ.get('BTS_ROOT',repo.parent)).resolve();snapshot=a.host_snapshot.resolve();out=a.output.resolve()
if not out.is_relative_to(bts/'build')or out in(bts/'build',snapshot)or out.is_relative_to(snapshot)or out.is_relative_to(bts/'build/host'):
 raise SystemExit('Use private output outside artist/snapshot storage')
if a.build_only and a.run_only:p.error('Choose build-only or run-only')
out.mkdir(parents=True,exist_ok=True);(out/'objects').mkdir(exist_ok=True);(out/'bin').mkdir(exist_ok=True)
def digest(path):
 result=hashlib.sha256()
 with path.open('rb')as stream:
  for block in iter(lambda:stream.read(1024*1024),b''):result.update(block)
 return result.hexdigest()
sources=('native/host/NativeMaterialGraph.cc','native/host/NativeMaterialRawHeight.hh','native/tests/NativeMaterialRawHeightFixture.hh','native/tests/native_raw_height_test.cc')
def source_contract():return {name:digest(repo/name)for name in(*sources,'scripts/host/verify-native-raw-height.py')}
source_hashes=source_contract()
env=dict(os.environ,CCACHE_DIR=str(bts/'build/ccache'))
if not a.run_only:
 baseline=json.loads((snapshot/'snapshot.json').read_text())['source_head']
 # No host layout/schema/header is modified by phase one. A snapshot older
 # than other merged features must not accidentally mix different consumers.
 changed=set(subprocess.check_output(['git','diff','--name-only',baseline,'--','native/host'],cwd=repo,text=True).splitlines())
 if changed-{'native/host/NativeMaterialGraph.cc','native/host/NativeMaterialRawHeight.hh'}:
  raise SystemExit('Snapshot native baseline differs beyond phase one; supply a matching fresh immutable host')
 commands=json.loads((snapshot/'host-commands.json').read_text());native=[]
 for command in commands:
  values=shlex.split(command)
  if '-c'in values and '/native/'in values[values.index('-c')+1]:native.append(values)
 template=next(values for values in native if values[values.index('-c')+1].endswith('/NativeMaterialGraph.cc'))
 old_object=template[template.index('-o')+1];source_root=template[template.index('-c')+1].split('/native/')[0]
 def compile(source,label):
  values=[value.replace(source_root+'/native',str(repo/'native'))for value in template];obj=out/'objects'/(label+'.o')
  values[values.index('-c')+1]=str(repo/source);values[values.index('-o')+1]=str(obj)
  for flag,replacement in(('-MF',str(obj)+'.d'),('-MT',str(obj))):
   if flag in values:values[values.index(flag)+1]=replacement
  first=next(i for i,v in enumerate(values)if v.startswith('-'));values[first:first]=['-I'+str(repo/'native/host')];values+=['-g0']
  print('Compile '+label,flush=True)
  with(out/(label+'.compile.log')).open('w')as log:subprocess.run(['nice','-n','19',*values],cwd=snapshot,env=env,stdout=log,stderr=subprocess.STDOUT,check=True)
  return obj
 graph=compile(sources[0],'bts-host-NativeMaterialGraph');driver=compile(sources[-1],'raw-height-driver')
 link=shlex.split(commands[-1]);link=link[2:]if link[:2]==[':','&&']else link
 if '&&'in link:link=link[:link.index('&&')]
 inputs={};args=[];main_replaced=False
 for value in link:
  if value==old_object:value=str(graph)
  elif value.endswith('/native/host/main.cc.o'):value=str(driver);main_replaced=True
  if value.endswith(('.o','.a')):
   path=Path(value);path=(path if path.is_absolute()else snapshot/path).resolve()
   if not path.is_file()or path.is_relative_to((bts/'build/host').resolve()):raise SystemExit('Missing/mutable link input: '+str(path))
   stat=path.stat();inputs[str(path)]={'size':stat.st_size,'mtime_ns':stat.st_mtime_ns,'sha256':digest(path)if path.is_relative_to(out)else None}
  value=value.replace(str(bts/'build/host/bin/lib'),str(snapshot/'bin/lib'))
  if value.startswith('-Wl,--dependency-file='):value='-Wl,--dependency-file='+str(out/'raw-height.link.d')
  args.append(value)
 if not main_replaced:raise SystemExit('Frozen host main was not replaced')
 args[args.index('-o')+1]=str(out/'bin/raw-height-test')
 print('Link raw-height-test',flush=True)
 with(out/'link.log').open('w')as log:subprocess.run(['nice','-n','19',*args],cwd=snapshot,env=env,stdout=log,stderr=subprocess.STDOUT,check=True)
 (out/'build.json').write_text(json.dumps({'baseline':baseline,'source_sha256':source_hashes,'link_inputs':inputs,'frozen_inputs_not_rehashed':True,'production_ndebug_flags_preserved':True,'existing_headers_layout_unchanged':True,'host_shader_and_program_implementation_unchanged':True,'binary_sha256':digest(out/'bin/raw-height-test')},indent=2)+'\n')
 for resource in a.host_resources.resolve().iterdir():
  if resource.is_dir()and resource.name[0].isdigit()and not(out/'bin'/resource.name).exists():(out/'bin'/resource.name).symlink_to(resource,target_is_directory=True)
 if source_hashes!=source_contract():raise SystemExit('Source changed during private build')
if a.build_only:raise SystemExit(0)
build=json.loads((out/'build.json').read_text());assert build['source_sha256']==source_hashes and build['binary_sha256']==digest(out/'bin/raw-height-test'),'Rebuild changed proof inputs first'
cases=json.loads(a.cases.read_text());assert isinstance(cases,list)and len(cases)==6
realpaths={case['file']:str(Path(case['file']).resolve())for case in cases}
originals={name:digest(Path(name))for name in dict.fromkeys(realpaths.values())}
run=out/('cpu'if a.cpu_only else'gpu');run.mkdir(exist_ok=True);private=run/'runtime'/uuid.uuid4().hex
for key,folder in(('XDG_CONFIG_HOME','config'),('XDG_DATA_HOME','data'),('XDG_CACHE_HOME','cache'),('XDG_STATE_HOME','state'),('TMPDIR','tmp'),('BTS_RECOVERY_DIR','recovery'),('BTS_RESOURCE_CACHE','resources'),('BTS_SESSION_LOG_DIR','logs')):
 (private/folder).mkdir(parents=True,exist_ok=True);env[key]=str(private/folder)
env.update(BTS_ROOT=str(bts),QT_QPA_PLATFORM='offscreen',BTS_HEADLESS_WORKSPACE='1',QT_PLUGIN_PATH=str(bts/'qt/6.8.3/plugins'),LD_LIBRARY_PATH=str(bts/'qt/6.8.3/lib')+':'+str(snapshot/'bin/lib')+':'+env.get('LD_LIBRARY_PATH',''))
wrapper=out/'raw-height-wrapper'
wrapper.write_text('#!/usr/bin/env python3\nimport os,sys\nassert sys.argv[1:3]==["--no-splash","--headless-native-raw-height-proof"]\nos.execv('+repr(str(out/'bin/raw-height-test'))+',["raw-height-test",*sys.argv[3:]])\n')
wrapper.chmod(0o755);env.update(BTS_HOST_EXECUTABLE=str(wrapper),BTS_ENVIRONMENT=str(bts/'build/environment-cache/TT_environmentLights_v001.hdr'))
if a.cpu_only:env['BTS_RAW_HEIGHT_CPU_ONLY']='1'
print('Run raw Height '+('CPU'if a.cpu_only else'CPU/GPU'),flush=True)
with(run/'run.log').open('w')as log:subprocess.run(['nice','-n','19','bash',str(repo/'scripts/host/run-host.sh'),'--no-splash','--headless-native-raw-height-proof',str(run),str(a.cases.resolve())],cwd=repo,env=env,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=300)
for _ in range(100):
 if 'BTS_SHUTDOWN clean'in(run/'run.log').read_text():break
 time.sleep(.02)
text=(run/'run.log').read_text();assert'BTS_SHUTDOWN clean'in text and'PASS native raw authored Height phase1'in text
assert not any(value in text for value in('Not freed memory blocks','Not freed memory','BTS_GPU_STACK_FALLBACK','Shader compilation failed','BTS_HOST_FAILED','OpenColorIO Error:','color_management | ERROR'))
proof=json.loads((run/'verified.json').read_text());assert proof['phase']==1 and proof['default_caps_unchanged']and proof['source_preserved']and not proof['ui_or_persistent_policy_activated']and not proof['cpu_interactive_composition']
assert sum(case['eligible']for case in proof['actual'])==1 and next(case for case in proof['actual']if case['eligible'])['material']=='Particle-Confetti'
if not a.cpu_only:assert proof['gpu']['maximum_analytic_error']<3e-5 and proof['gpu']['maximum_native_error']<3e-5 and proof['gpu']['owners']==2
assert all(digest(Path(name))==sha for name,sha in originals.items()),'Original source changed'
assert all(str(Path(name).resolve())==resolved for name,resolved in realpaths.items()),'Original source alias changed'
(run/'evidence.json').write_text(json.dumps({'binary_sha256':build['binary_sha256'],'source_sha256':source_hashes,'original_files_sha256':originals,'proof_sha256':digest(run/'verified.json'),'clean_exit':True,'cpu_only':a.cpu_only},indent=2)+'\n')
print('Native authored FLOAT Height phase1 PASS',flush=True)
