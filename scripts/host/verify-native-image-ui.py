"""Build a coherent private host ABI and verify native image replacement without desktop input.

By default every native translation unit is rebuilt. A verified complete Ninja
include audit can select all changed consumers and reuse unaffected snapshot
objects. Native archives retain target identity and are rebuilt privately.
Coordinate build/GPU slots; ordinary commit guards are separate.
"""
import argparse
import concurrent.futures
import hashlib
import json
import os
import re
from pathlib import Path
import shlex
import subprocess
import uuid

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--host-snapshot',type=Path,required=True)
p.add_argument('--host-resources',type=Path,required=True)
p.add_argument('--output',type=Path,required=True)
p.add_argument('--build-only',action='store_true')
p.add_argument('--run-only',action='store_true')



p.add_argument('--jobs',type=int,choices=(1,2),default=2)

p.add_argument('--consumer-audit',type=Path,help='Verified complete Ninja include closure matching this immutable snapshot; default rebuilds all native units')
a=p.parse_args();a.cpu_only=False;a.sanitize_cpu=False;repo=Path(__file__).resolve().parents[2];bts=Path(os.environ.get('BTS_ROOT',repo.parent)).resolve();snapshot=a.host_snapshot.resolve();out=a.output.resolve()


if not out.is_relative_to(bts/'build')or out in(bts/'build',snapshot)or out.is_relative_to(snapshot)or out.is_relative_to(bts/'build/host'):
 raise SystemExit('Output must be private build storage outside host/snapshot')
out.mkdir(parents=True,exist_ok=True)
for folder in('bin','objects','tmp'):(out/folder).mkdir(exist_ok=True)
env=dict(os.environ,CCACHE_DIR=str(bts/'build/ccache'),TMPDIR=str(out/'tmp'))
if os.name=='posix':
 import resource
 resource.setrlimit(resource.RLIMIT_CORE,(0,0))
if not a.run_only:
 commands=json.loads((snapshot/'host-commands.json').read_text());native=[]
 for command in commands:
  args=shlex.split(command)
  if '-c'in args and '/native/'in args[args.index('-c')+1]:native.append(args)
 source_root=Path(native[0][native[0].index('-c')+1].split('/native/')[0])
 def compile(source_args,source,label,test_assert=False):
  args=[value.replace(str(source_root/'native'),str(repo/'native'))for value in source_args]
  obj=out/'objects'/(label+'.o');args[args.index('-o')+1]=str(obj);args[args.index('-c')+1]=str(source)
  for option,value in(('-MF',str(obj)+'.d'),('-MT',str(obj))):
   if option in args:args[args.index(option)+1]=value
  # Keep all production optimization flags; source/headers and parameter layout
  # are identical for the host and production-main artist driver.
  first=next(i for i,value in enumerate(args)if value.startswith('-'));args[first:first]=['-I'+str(repo/'native/host')]
  args.extend((['-UNDEBUG']if test_assert else[])+['-g0'])
  if a.sanitize_cpu:args=['-O1'if value in('-O2','-O3')else value for value in args]+['-fsanitize=address,undefined','-fno-omit-frame-pointer']
  print('Compile '+label,flush=True)
  with(out/(label+'.compile.log')).open('w')as log:subprocess.run(['nice','-n','19',*args],cwd=snapshot,env=env,stdout=log,stderr=subprocess.STDOUT,check=True)
  return str(obj)
 template=next(args for args in native if args[args.index('-c')+1].endswith('/MeshBaker.cc'))
 cpu={}
 for name in('native_material_image_variant_jobs_test',):
  cpu[name]=compile(template,repo/'native/tests'/(name+'.cc'),name,True)
 objects={};selected=native;closure=None
 # New public methods/types still require the complete transitive include
 # closure, including archive targets. Never guess ABI safety by basename.
 frozen=json.loads((snapshot/'snapshot.json').read_text())['source_head']
 changed=subprocess.check_output(['git','diff','--name-only',frozen,'--','native/host'],cwd=repo,text=True).splitlines()
 new_sources={path for path in changed if path.endswith('.cc') and subprocess.run(['git','cat-file','-e',frozen+':'+path],cwd=repo,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL).returncode!=0}
 assert new_sources<={"native/host/NativeMaterialImageJobs.cc","native/host/NativeMaterialImagePanel.cc","native/host/NativeMaterialImageUi.cc"},'Unexpected new production unit; extend the explicit build closure'
 if not a.consumer_audit and (snapshot/'native-deps.txt').is_file():
  header_paths={path for path in changed if path.endswith('.hh') and subprocess.run(['git','cat-file','-e',frozen+':'+path],cwd=repo,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL).returncode==0}
  deps={};key=None
  for line in (snapshot/'native-deps.txt').read_text().splitlines():
   if line and not line[0].isspace():
    assert '(VALID)' in line,'Stale Ninja dependency record';key=line.split(': #deps ',1)[0];deps[key]=set()
   elif key and line.strip():deps[key].add(line.strip())
  keys={args[args.index('-o')+1] for args in native};assert keys==set(deps),'Incomplete target-qualified dependency snapshot'
  units=[]
  for args in native:
   obj=args[args.index('-o')+1];source=str(Path(args[args.index('-c')+1]).relative_to(source_root));hits=sorted(Path(path).name for path in header_paths if any(dep.endswith('/'+path)for dep in deps[obj]))
   if hits or source in changed:units.append({'source':source,'object':obj,'changed_headers':hits,'direct_source_changed':source in changed})
  report={'frozen_source':frozen,'native_compile_units':len(native),'worktrees':{repo.name:{'changed_headers':sorted(Path(path).name for path in header_paths),'affected_existing_units':len(units),'units':units}}}
  a.consumer_audit=out/'consumer-audit.json';a.consumer_audit.write_text(json.dumps(report,indent=2)+'\n')
 if not a.cpu_only:
  if a.consumer_audit:
   audit=json.loads(a.consumer_audit.read_text());frozen=json.loads((snapshot/'snapshot.json').read_text())['source_head'];assert frozen.startswith(audit['frozen_source'])and audit['native_compile_units']==len(native)
   closure=audit['worktrees'][repo.name];keys={unit['object']for unit in closure['units']};assert len(keys)==closure['affected_existing_units']
   changed=subprocess.check_output(['git','diff','--name-only',audit['frozen_source'],'--','native/host'],cwd=repo,text=True).splitlines()
   header_paths={path for path in changed if path.endswith('.hh')}
   original_headers={path for path in header_paths if subprocess.run(['git','cat-file','-e',audit['frozen_source']+':'+path],cwd=repo,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL).returncode==0}
   assert {Path(path).name for path in original_headers}==set(closure['changed_headers']),'Regenerate include closure after existing header edits'
   covered_sources={unit['source']for unit in closure['units']};changed_sources={path for path in changed if path.endswith('.cc')};assert changed_sources-new_sources<=covered_sources,'Regenerate include closure after source edits'
   # New headers have no baseline Ninja dependency record. Every include edge
   # must lead through an already covered source/header, including new wrappers.
   new_headers=header_paths-original_headers;covered=set(original_headers)|covered_sources|new_sources
   pending=set(new_headers)
   while pending:
    resolved=set()
    for path in pending:
     pattern=re.compile(r'^\s*#\s*include\s*"'+re.escape(Path(path).name)+r'"',re.M)
     parents={str(parent.relative_to(repo))for parent in[*(repo/'native/host').rglob('*'),*(repo/'native/harness').rglob('*')]if parent.is_file()and parent.suffix in('.hh','.cc','.inc')and pattern.search(parent.read_text())}
     if parents and parents<=covered:resolved.add(path)
    assert resolved,'New header has an uncovered include edge; use full rebuild or regenerate audit'
    pending-=resolved;covered|=resolved
   selected=[args for args in native if args[args.index('-o')+1]in keys];assert len(selected)==len(keys)
   for args in native:
    old=args[args.index('-o')+1]
    if old not in keys and(snapshot/old).is_file():objects[old]=str((snapshot/old).resolve())
  def production(args):
   original=args[args.index('-o')+1];source=repo/Path(args[args.index('-c')+1]).relative_to(source_root);target=next(part[:-4]for part in Path(original).parts if part.endswith('.dir'));label=target+'-'+source.stem+'-'+hashlib.sha256(original.encode()).hexdigest()[:12]
   return original,compile(args,source,label)
  with concurrent.futures.ThreadPoolExecutor(max_workers=a.jobs)as workers:
   for old,new in workers.map(production,selected):objects[old]=new
  main=next(args for args in native if args[args.index('-c')+1].endswith('/main.cc'))
  artist=compile(main,repo/'native/tests/native_material_image_ui_artist_test.cc','native-image-artist')
  extra=[]
  for name in('NativeMaterialImageJobs','NativeMaterialImagePanel','NativeMaterialImageUi'):
   extra.append(compile(main,repo/'native/host'/(name+'.cc'),'bts-host-new-'+name))
 archives={};archive_members={};native_keys={args[args.index('-o')+1]for args in native};rebuilt_keys={args[args.index('-o')+1]for args in selected}if not a.cpu_only else set()
 if not a.cpu_only:
  (out/'lib').mkdir(exist_ok=True)
  for command in commands:
   if ' qc 'not in command:continue
   args=shlex.split(command)
   position=next((i for i,value in enumerate(args)if Path(value).name in('ar','gcc-ar','llvm-ar')),None)
   if position is None:continue
   archive_args=args[position:]
   if '&&'in archive_args:archive_args=archive_args[:archive_args.index('&&')]
   old=archive_args[2];members=archive_args[3:]
   if not any(member in native_keys for member in members):continue
   if old in archives:continue
   frozen_archive=(snapshot/old).resolve()
   if not frozen_archive.is_file():raise SystemExit('Missing immutable native archive: '+str(frozen_archive))
   if not any(member in rebuilt_keys for member in members):
    archives[old]=str(frozen_archive);archive_members[old]={'archive':str(frozen_archive),'unchanged':True};continue
   updated=[]
   for member in members:
    if member in rebuilt_keys and member not in objects:raise SystemExit('Unrebuilt native archive ABI consumer: '+member)
    value=objects.get(member,member);path=Path(value);path=(path if path.is_absolute()else snapshot/path).resolve()
    if not path.is_file():
     # Link snapshots contain the immutable archive, not necessarily its .o members.
     # Only audited unaffected members may be recovered from that exact archive.
     if not closure or member in rebuilt_keys:raise SystemExit('Missing rebuilt archive consumer: '+member)
     basename=Path(member).name
     if sum(Path(other).name==basename for other in members)!=1:raise SystemExit('Ambiguous frozen archive member: '+basename)
     path=out/'lib'/'members'/Path(old).stem/basename;path.parent.mkdir(parents=True,exist_ok=True)
     with path.open('wb')as stream:subprocess.run(['nice','-n','19',archive_args[0],'p',str(frozen_archive),basename],stdout=stream,check=True)
     if not path.stat().st_size:raise SystemExit('Empty frozen archive member: '+basename)
     if member in native_keys:objects[member]=str(path)
    if path.is_relative_to((bts/'build/host').resolve()):raise SystemExit('Mutable archive input: '+str(path))
    updated.append(str(path))
   archive=out/'lib'/Path(old).name
   if archive.exists():archive.unlink()
   print('Archive '+archive.name,flush=True)
   with(out/(archive.name+'.archive.log')).open('w')as log:
    subprocess.run(['nice','-n','19',archive_args[0],archive_args[1],str(archive),*updated],cwd=snapshot,env=env,stdout=log,stderr=subprocess.STDOUT,check=True)
    subprocess.run(['nice','-n','19','ranlib',str(archive)],cwd=snapshot,env=env,stdout=log,stderr=subprocess.STDOUT,check=True)
   archives[old]=str(archive);archive_members[old]={'archive':str(archive),'members':updated}
  (out/'coherent-native-archives.json').write_text(json.dumps(archive_members,indent=2)+'\n')
 link=shlex.split(commands[-1]);link=link[2:]if link[:2]==[':','&&']else link
 if '&&'in link:link=link[:link.index('&&')]
 for value in link:
  if value.endswith(('.o','.a')):
   path=Path(value);path=(path if path.is_absolute()else snapshot/path).resolve()
   if not path.is_file()or path.is_relative_to((bts/'build/host').resolve()):raise SystemExit('Non-frozen/missing link input: '+str(path))
 def link_binary(name,overrides,driver=None,cpu=False):
  args=[]
  for value in link:
   if cpu and value.endswith('.o'):continue
   if not cpu and value.endswith('.o')and '/native/'in value and value not in overrides:raise SystemExit('Unrebuilt native host ABI input: '+value)
   if not cpu and value.startswith('lib/libbts')and value.endswith('.a')and value not in archives:raise SystemExit('Unrebuilt native ABI archive: '+value)
   if driver and value.endswith('/GuiFixDemo.cc.o'):value=driver
   else:value=archives.get(value,overrides.get(value,value))
   value=value.replace(str(source_root.parent/'build/host/bin/lib'),str(snapshot/'bin/lib')).replace(str(bts/'build/host/bin/lib'),str(snapshot/'bin/lib'))
   if value.startswith('-Wl,--dependency-file='):value='-Wl,--dependency-file='+str(out/(name+'.link.d'))
   args.append(value)
  args[1:1]=extra
  if cpu:args.insert(next(i for i,value in enumerate(args)if value.endswith('.a')),driver)
  args[args.index('-o')+1]=str(out/'bin'/name)
  print('Link '+name,flush=True)
  with(out/(name+'.link.log')).open('w')as log:subprocess.run(['nice','-n','19',*args],cwd=snapshot,env=env,stdout=log,stderr=subprocess.STDOUT,check=True)
 if not a.cpu_only:
  link_binary('bts-host',objects);link_binary('native-image-artist',objects,artist)
  fixture=dict(objects);main_key=next(key for key in objects if key.endswith('/main.cc.o'));fixture[main_key]=cpu['native_material_image_variant_jobs_test'];link_binary('native-image-jobs',fixture)
  manifest={old:{'object':new,'sha256':hashlib.sha256(Path(new).read_bytes()).hexdigest()}for old,new in objects.items()}
  (out/'coherent-host-objects.json').write_text(json.dumps(manifest,indent=2)+'\n')
 for resource_path in a.host_resources.resolve().iterdir():
  if resource_path.is_dir()and resource_path.name[0].isdigit()and not(out/'bin'/resource_path.name).exists():(out/'bin'/resource_path.name).symlink_to(resource_path,target_is_directory=True)
 (out/'build.json').write_text(json.dumps({'source':str(repo),'snapshot':str(snapshot),'host_native_inputs':len(objects),'native_compile_units':len(native),'host_native_recompiled':len(selected)if not a.cpu_only else 0,'include_closure':closure,'target_qualified_objects':True,'native_archives_rebuilt':[old for old,value in archive_members.items()if not value.get('unchanged')],'native_archives_reused':[old for old,value in archive_members.items()if value.get('unchanged')],'all_host_parameter_consumers_coherent':not a.cpu_only,'production_ndebug_flags_preserved':True,'source_head':subprocess.check_output(['git','rev-parse','HEAD'],cwd=repo,text=True).strip()},indent=2)+'\n')
if a.build_only:raise SystemExit(0)
build=json.loads((out/'build.json').read_text());assert build['target_qualified_objects'] and (build['native_archives_rebuilt'] or build['native_archives_reused'])
env.update(BTS_ROOT=str(bts),QT_QPA_PLATFORM='offscreen',BTS_HEADLESS_WORKSPACE='1',QT_PLUGIN_PATH=str(bts/'qt/6.8.3/plugins'),LD_LIBRARY_PATH=str(bts/'qt/6.8.3/lib')+':'+str(snapshot/'bin/lib')+':'+str(a.host_resources.resolve()/'lib')+(':'+env['LD_LIBRARY_PATH'] if env.get('LD_LIBRARY_PATH')else''))
def private(directory):
 result=dict(env);directory=directory/'runtime'/uuid.uuid4().hex
 for key,folder in(('XDG_CONFIG_HOME','config'),('XDG_DATA_HOME','data'),('XDG_CACHE_HOME','cache'),('XDG_STATE_HOME','state'),('TMPDIR','tmp'),('BTS_RECOVERY_DIR','recovery'),('BTS_SESSION_LOG_DIR','logs'),('BTS_RESOURCE_CACHE','resources')):
  (directory/folder).mkdir(parents=True,exist_ok=True);result[key]=str(directory/folder)
 return result
fixture=out/'fixtures';fixture_env=private(out/'fixture-data');fixture_env['BTS_NATIVE_IMAGE_FIXTURE_ONLY']='1'
with(out/'fixture.log').open('w') as log:subprocess.run(['nice','-n','19',str(out/'bin/native-image-jobs'),str(fixture)],env=fixture_env,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=120)
with(out/'jobs.log').open('w') as log:subprocess.run(['nice','-n','19',str(out/'bin/native-image-jobs'),str(out/'jobs-fixtures')],env=private(out/'jobs-data'),stdout=log,stderr=subprocess.STDOUT,check=True,timeout=120)
mesh=out/'source-mesh.obj';mesh.write_text('''mtllib source-mesh.mtl
o SourceObject
v -1 0 0
v 0 0 0
v 0 1 0
v -1 1 0
v 0 0 0
v 1 0 0
v 1 1 0
v 0 1 0
vt 0 0
vt 1 0
vt 1 1
vt 0 1
usemtl A_Source
f 1/1 2/2 3/3 4/4
o DependentObject
usemtl B_Dependent
f 5/1 6/2 7/3 8/4
''');mesh.with_suffix('.mtl').write_text('newmtl A_Source\nKd .2 .3 .4\nnewmtl B_Dependent\nKd .4 .3 .2\n')
oracle=fixture/'artist-oracle.json';metadata=json.loads(oracle.read_text());project=out/'artist/replacement.blend';proofs={}
for cold in(False,True):
 directory=out/('cold-open' if cold else 'artist');directory.mkdir(exist_ok=True);runtime=private(directory)
 runtime.update(BTS_HOST_EXECUTABLE=str(out/'bin/native-image-artist'),BTS_NATIVE_IMAGE_UI_DEMO='1',BTS_BLEND_MATERIAL_DEMO='1',BTS_AE_PERSISTENCE_DEMO='1',BTS_VERIFY_ASYNC_SAVE='1',BTS_NATIVE_IMAGE_LIBRARY=str(fixture/'source.blend'),BTS_NATIVE_IMAGE_REPLACEMENT=str(fixture/'replacement.png'),BTS_NATIVE_IMAGE_PROJECT=str(project),BTS_NATIVE_IMAGE_ORACLE=str(oracle),BTS_NATIVE_IMAGE_EXPECTED=str(out/'artist/expected-cold.json'),BTS_TEMPLATE_DIR=str(directory/'templates'),BTS_ENVIRONMENT=env.get('BTS_ENVIRONMENT',str(bts/'build/environment-cache/TT_environmentLights_v001.hdr')))
 hidden=out/'fixtures-source-hidden'
 if cold:
  # Oracle bytes are test metadata; every actual encoded source is hidden.
  copied=out/'cold-oracle.json';copied.write_bytes(oracle.read_bytes());runtime['BTS_NATIVE_IMAGE_ORACLE']=str(copied);runtime['BTS_NATIVE_IMAGE_REOPEN']='1';fixture.rename(hidden)
 try:
  with(directory/'run.log').open('w') as log:subprocess.run(['nice','-n','19','bash',str(repo/'scripts/host/run-host.sh'),'--no-splash','--workspace','--gui-fix-demo',str(directory),'--gui-fix-mesh',str(mesh),'--gui-fix-resolution','128','--autosave-seconds','3600','--library-dir',str(Path(runtime['XDG_DATA_HOME'])/'library')],cwd=repo,env=runtime,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=360)
 finally:
  if cold:hidden.rename(fixture)
 text=(directory/'run.log').read_text();assert 'BTS_SHUTDOWN clean' in text and 'NATIVE_IMAGE_ARTIST_PASS' in text
 for forbidden in('Not freed memory blocks','BTS_GPU_STACK_FALLBACK','BTS_GPU_AUTHORING_CPU_READ','BTS_DRAW_FAILED','BTS_HOST_FAILED','Shader compilation failed'):assert forbidden not in text,forbidden
 proofs['cold_open' if cold else 'artist']=json.loads((directory/'native-image-artist.json').read_text())
for name,key in(('source.blend','library_sha256'),('source.png','source_sha256'),('replacement.png','replacement_sha256')):assert hashlib.sha256((fixture/name).read_bytes()).hexdigest()==metadata[key]
(out/'verified.json').write_text(json.dumps({'source_bytes_unchanged':True,'fixture_sha256':metadata,**proofs},indent=2)+'\n')
print('Native image UI file/Shelf, atomic failures, one Undo, Reset and cold packed Open PASS')
