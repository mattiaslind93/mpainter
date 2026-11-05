"""Build a coherent private host ABI and verify linked Mapping without desktop input.

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
p.add_argument('--resume-gpu-proofs',action='store_true',help='With --run-only, reuse a verified GPU checkpoint only when host/proof/source SHA256 still match')
p.add_argument('--cpu-only',action='store_true')
p.add_argument('--preflight-only',action='store_true',help='Run CPU transport, real source-graph and widget checks; no GPU context')
p.add_argument('--sanitize-cpu',action='store_true',help='ASan/UBSan for the pure CPU transport tests; requires --cpu-only')
p.add_argument('--upstream-mapping',action='store_true',help='Retained Blender Mapping source + extra POINT, including live Reset/Undo and restart')
p.add_argument('--jobs',type=int,choices=(1,2),default=2)
p.add_argument('--cases',type=Path,help='Own-source case manifest or native-material-inspection JSON for actual Cardboard/Clay/Stone')
p.add_argument('--consumer-audit',type=Path,help='Verified complete Ninja include closure matching this immutable snapshot; default rebuilds all native units')
a=p.parse_args();repo=Path(__file__).resolve().parents[2];bts=Path(os.environ.get('BTS_ROOT',repo.parent)).resolve();snapshot=a.host_snapshot.resolve();out=a.output.resolve()
if a.resume_gpu_proofs and not a.run_only:p.error('--resume-gpu-proofs requires --run-only')
if a.sanitize_cpu and not a.cpu_only:p.error('--sanitize-cpu requires --cpu-only; it does not sanitize the host')
if not out.is_relative_to(bts/'build')or out in(bts/'build',snapshot)or out.is_relative_to(snapshot)or out.is_relative_to(bts/'build/host'):
 raise SystemExit('Output must be private build storage outside host/snapshot')
out.mkdir(parents=True,exist_ok=True)
for folder in('bin','objects','tmp'):(out/folder).mkdir(exist_ok=True)
env=dict(os.environ,CCACHE_DIR=str(bts/'build/ccache'),TMPDIR=str(out/'tmp'))
if a.upstream_mapping:env['BTS_NATIVE_MAPPING_UPSTREAM']='1'
if os.name=='posix':
 import resource
 resource.setrlimit(resource.RLIMIT_CORE,(0,0))
def source_contract():
 return {str(path.relative_to(repo)):hashlib.sha256(path.read_bytes()).hexdigest()for path in sorted((repo/'native').rglob('*'))if path.is_file()and path.suffix in('.hh','.h','.cc','.inc','.py')}
if not a.run_only:
 source_hashes=source_contract()
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
 for name in('native_material_mapping_test','native_material_parameters_test'):
  cpu[name]=compile(template,repo/'native/tests'/(name+'.cc'),name,True)
 objects={};selected=native;closure=None
 if not a.cpu_only:
  if a.consumer_audit:
   audit=json.loads(a.consumer_audit.read_text());frozen=json.loads((snapshot/'snapshot.json').read_text())['source_head'];assert frozen.startswith(audit['frozen_source'])and audit['native_compile_units']==len(native)
   closure=audit['worktrees'][repo.name];keys={unit['object']for unit in closure['units']};assert len(keys)==closure['affected_existing_units']
   changed=subprocess.check_output(['git','diff','--name-only',audit['frozen_source'],'--','native/host'],cwd=repo,text=True).splitlines()
   header_paths={path for path in changed if path.endswith('.hh')}
   original_headers={path for path in header_paths if subprocess.run(['git','cat-file','-e',audit['frozen_source']+':'+path],cwd=repo,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL).returncode==0}
   assert {Path(path).name for path in original_headers}==set(closure['changed_headers']),'Regenerate include closure after existing header edits'
   covered_sources={unit['source']for unit in closure['units']};changed_sources={path for path in changed if path.endswith('.cc')};assert changed_sources<=covered_sources,'Regenerate include closure after source edits'
   # New headers have no baseline Ninja dependency record. Every include edge
   # must lead through an already covered source/header, including new wrappers.
   new_headers=header_paths-original_headers;covered=set(original_headers)|covered_sources
   pending=set(new_headers)
   while pending:
    resolved=set()
    for path in pending:
     pattern=re.compile(r'^\s*#\s*include\s*"'+re.escape(Path(path).name)+r'"',re.M)
     parents={str(parent.relative_to(repo))for parent in(repo/'native/host').iterdir()if parent.is_file()and parent.suffix in('.hh','.cc','.inc')and pattern.search(parent.read_text())}
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
  artist=compile(main,repo/'native/tests/native_material_mapping_artist_test.cc','mapping-artist')
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
   if driver and value.endswith('/main.cc.o'):value=driver
   else:value=archives.get(value,overrides.get(value,value))
   value=value.replace(str(source_root.parent/'build/host/bin/lib'),str(snapshot/'bin/lib')).replace(str(bts/'build/host/bin/lib'),str(snapshot/'bin/lib'))
   if value.startswith('-Wl,--dependency-file='):value='-Wl,--dependency-file='+str(out/(name+'.link.d'))
   args.append(value)
  if cpu:args.insert(next(i for i,value in enumerate(args)if value.endswith('.a')),driver)
  args[args.index('-o')+1]=str(out/'bin'/name)
  print('Link '+name,flush=True)
  with(out/(name+'.link.log')).open('w')as log:subprocess.run(['nice','-n','19',*args],cwd=snapshot,env=env,stdout=log,stderr=subprocess.STDOUT,check=True)
 for name,obj in cpu.items():
  # These two transport tests use Qt Core and header-only resource admission;
  # no host objects (and therefore no old/new host ABI) enter their binaries.
  args=['c++',obj,'-o',str(out/'bin'/name),'-L'+str(bts/'qt/6.8.3/lib'),'-lQt6Core','-Wl,-rpath,'+str(bts/'qt/6.8.3/lib')]
  if a.sanitize_cpu:args+=['-fsanitize=address,undefined']
  with(out/(name+'.link.log')).open('w')as log:subprocess.run(['nice','-n','19',*args],cwd=snapshot,env=env,stdout=log,stderr=subprocess.STDOUT,check=True)
 if not a.cpu_only:
  link_binary('bts-host',objects);link_binary('mapping-artist',objects,artist)
  manifest={old:{'object':new,'sha256':hashlib.sha256(Path(new).read_bytes()).hexdigest()}for old,new in objects.items()}
  (out/'coherent-host-objects.json').write_text(json.dumps(manifest,indent=2)+'\n')
 for resource_path in a.host_resources.resolve().iterdir():
  if resource_path.is_dir()and resource_path.name[0].isdigit()and not(out/'bin'/resource_path.name).exists():(out/'bin'/resource_path.name).symlink_to(resource_path,target_is_directory=True)
 (out/'build.json').write_text(json.dumps({'source':str(repo),'snapshot':str(snapshot),'host_native_inputs':len(objects),'native_compile_units':len(native),'host_native_recompiled':len(selected)if not a.cpu_only else 0,'include_closure':closure,'target_qualified_objects':True,'native_archives_rebuilt':[old for old,value in archive_members.items()if not value.get('unchanged')],'native_archives_reused':[old for old,value in archive_members.items()if value.get('unchanged')],'all_host_parameter_consumers_coherent':not a.cpu_only,'production_ndebug_flags_preserved':True,'source_sha256':source_hashes,'source_head':subprocess.check_output(['git','rev-parse','HEAD'],cwd=repo,text=True).strip()},indent=2)+'\n')
if not a.run_only and source_hashes!=source_contract():raise SystemExit('Source changed during private build; rebuild before verification')
if a.build_only:raise SystemExit(0)
if not a.cpu_only:
 build=json.loads((out/'build.json').read_text());assert build.get('source_sha256')==source_contract(),'Native source changed since coherent build; rebuild first'
 assert build.get('target_qualified_objects')and build.get('native_archives_rebuilt'),'Rebuild with target-qualified native objects and private native archives first'
 assert build.get('production_ndebug_flags_preserved'),'Production debug mode must match frozen Blender'
 allowed={Path(value['object']).resolve()for value in json.loads((out/'coherent-host-objects.json').read_text()).values()}
 for binary in('bts-host','mapping-artist'):
  dependencies=(out/(binary+'.link.d')).read_text()
  for value in re.findall(r'(?<!\S)([^\s\\]+\.o)(?=\s|$)',dependencies):
   if '/native/'in value:
    path=Path(value);path=(path if path.is_absolute()else snapshot/path).resolve();assert path in allowed,'Unexpected native host ABI object: '+str(path)
env.update(BTS_ROOT=str(bts),QT_QPA_PLATFORM='offscreen',BTS_HEADLESS_WORKSPACE='1',QT_PLUGIN_PATH=str(bts/'qt/6.8.3/plugins'),LD_LIBRARY_PATH=str(bts/'qt/6.8.3/lib')+':'+str(snapshot/'bin/lib')+':'+str(a.host_resources.resolve()/'lib')+(':'+env['LD_LIBRARY_PATH']if env.get('LD_LIBRARY_PATH')else''))
if a.sanitize_cpu:env.update(ASAN_OPTIONS='detect_leaks=1:abort_on_error=1',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
runtime_id=uuid.uuid4().hex
def private(directory):
 result=dict(env)
 directory=directory/'runtime'/runtime_id
 for key,folder in(('XDG_CONFIG_HOME','config'),('XDG_DATA_HOME','data'),('XDG_CACHE_HOME','cache'),('XDG_STATE_HOME','state'),('TMPDIR','tmp'),('BTS_RECOVERY_DIR','recovery'),('BTS_SESSION_LOG_DIR','logs'),('BTS_RESOURCE_CACHE','resources')):
  (directory/folder).mkdir(parents=True,exist_ok=True);result[key]=str(directory/folder)
 return result
for name in('native_material_mapping_test','native_material_parameters_test'):
 with(out/(name+'.run.log')).open('w')as log:subprocess.run(['nice','-n','19',str(out/'bin'/name)],env=private(out/(name+'-data')),stdout=log,stderr=subprocess.STDOUT,check=True,timeout=60)
if a.cpu_only:print('Native Mapping CPU transport/bounds PASS');raise SystemExit(0)
panel_dir=out/'panel';panel_dir.mkdir(exist_ok=True);panel_env=private(panel_dir);panel_env.pop('BTS_NATIVE_MATERIAL_CASES',None)
with(panel_dir/'run.log').open('w')as log:subprocess.run(['nice','-n','19',str(out/'bin/bts-host'),'--no-splash','--cpu-native-material-test',str(panel_dir)],cwd=repo,env=panel_env,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=120)
panel=json.loads((panel_dir/'native-material-panel.json').read_text());
vector_graph=panel['upstream_mapping_graph']['vector_math'];assert vector_graph['supported_sources']==12 and vector_graph['actual_operation_or_socket_rejections']==3 and vector_graph['cache_install_order_cases']==2 and vector_graph['actual_hidden_source_archive_hydration'] and vector_graph['legacy_runtime_mapping_adapters']==0 and vector_graph['prepared_current_and_legacy_height_resources'] and vector_graph['gpu_compilations']==0
assert panel['vector_math_mapping_gestures']==2
assert panel['linked_mapping_gestures']==4 and panel['linked_mapping_cow']and panel['linked_mapping_reset_exact']and panel['linked_mapping_degrees_to_radians']
if a.upstream_mapping:
 graph=panel['upstream_mapping_graph'];assert graph['source_graph_cases']==8 and graph['unsupported_context_channels']==2 and graph['rejected_forged_or_invalid_sources']==5 and graph['inactive_original_edges_identity_preserved_and_override_rejected']==8 and graph['original_node_mute_preserved'] and graph['unused_and_unsupported_only_new_edit_rejections']==4 and graph['production_queue_admission_cases']==4 and graph['legacy_unused_and_inactive_schema3_preserved'] and graph['variant_uuid_changed_rows_and_unrelated_interface_admitted_correctly'] and graph['cpu_gpu_contexts']==0 and graph['cpu_gpu_programs_created']==0 and graph['cpu_retired_sources_drained_before_blender_teardown'] and graph['legacy_reset_undo_redo_admission_cases']==12 and graph['history_restore_keeps_instance_corruption_guards']
if a.preflight_only:print('Native Mapping CPU transport + retained source graph + widget preflight PASS');raise SystemExit(0)
gpu_env=private(out/'gpu');gpu_env['BTS_HOST_EXECUTABLE']=str(out/'bin/bts-host');gpu_args=[];source_hashes={}
def digest(path):
 result=hashlib.sha256()
 with path.open('rb')as stream:
  for chunk in iter(lambda:stream.read(1024*1024),b''):result.update(chunk)
 return result.hexdigest()
if a.cases:
 cases=json.loads(a.cases.read_text());cases=cases.get('cases',[])if isinstance(cases,dict)else cases
 cases=[dict(file=case.get('file',case.get('source')),material=case['material'],linked_mapping=True)for case in cases if case['material']in('4k-Cardboard-Damaged','4k-Clay','4k-Stylized-Stone')]
 assert len(cases)==3 and len({case['material']for case in cases})==3
 source_hashes={filename:digest(Path(filename))for filename in dict.fromkeys(case['file']for case in cases)}
 (out/'mapping-cases.json').write_text(json.dumps(cases,indent=2)+'\n');gpu_args=['--cases',str(out/'mapping-cases.json'),'--decode-images','--archive-roundtrip']
checkpoint=out/'gpu-checkpoint.json'
if a.resume_gpu_proofs:
 previous=json.loads(checkpoint.read_text());assert previous=={'host_sha256':digest(out/'bin/bts-host'),'proof_sha256':digest(out/'gpu/verified.json'),'source_sha256':source_hashes},'GPU checkpoint inputs changed; run full GPU verification'
else:
 with(out/'gpu-wrapper.log').open('w')as log:subprocess.run(['nice','-n','19','python3',str(repo/'scripts/host/verify-native-material-gpu.py'),'--output',str(out/'gpu'),'--timeout','300',*gpu_args],cwd=repo,env=gpu_env,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=330)
surface=json.loads((out/'gpu/verified.json').read_text())['surface_program'];
vector=surface['linked_mapping']['retained_vector_math'];assert len(vector['cases'])==6 and vector['exact_identity_components']>0 and vector['analytic_components']>0 and vector['exact_reset_components']==vector['exact_identity_components'] and vector['maximum_error']<3e-5 and vector['fixed_bound']==3e-5 and vector['shader_recompiles']==0 and vector['height_lookup_layouts']==2 and vector['cross_layout_height_rejected'] and vector['hidden_source_archive_hydration'] and vector['height_components']>0 and vector['height_maximum_error']<3e-5
assert surface['linked_mapping']['exact_identity_components']>1000 and surface['linked_mapping']['maximum_affine_error']<3e-5
if a.upstream_mapping:
 upstream=surface['linked_mapping']['retained_upstream_mapping'];assert len(upstream['cases'])==16 and upstream['maximum_error']<3e-5 and upstream['exact_identity_components']>50000 and upstream['shader_recompiles']==0 and upstream['muted_node_exact_identity_components']>0 and upstream['muted_node_analytic_components']>0 and upstream['muted_node_maximum_error']<3e-5 and upstream['legacy_inactive_unused_cases']==4 and upstream['legacy_exact_noop_components']>0
actual=[]
if a.cases:
 actual=json.loads((out/'gpu/verified.json').read_text())['real_groups'];assert len(actual)==3
 for case in actual:assert case['linked_mapping']['components']>0 and case['linked_mapping']['maximum_error']<3e-5 and case['archive']['input_interface_exact']
 for filename,sha in source_hashes.items():assert digest(Path(filename))==sha
checkpoint.write_text(json.dumps({'host_sha256':digest(out/'bin/bts-host'),'proof_sha256':digest(out/'gpu/verified.json'),'source_sha256':source_hashes},indent=2)+'\n')
fixture=out/'fixtures/mapping.blend'
with(out/'fixture.log').open('w')as log:subprocess.run(['nice','-n','19',str(bts/'build/baseline/bin/blender'),'--background','--factory-startup','--python',str(repo/'native/tests/native_material_mapping_fixture.py'),'--',str(fixture)],env=private(out/'fixture-data'),stdout=log,stderr=subprocess.STDOUT,check=True,timeout=90)
source_sha=hashlib.sha256(fixture.read_bytes()).hexdigest();project=out/'artist/mapping.blend';proofs={}
if project.exists():project.unlink()
for cold in(False,True):
 directory=out/('cold-open'if cold else'artist');directory.mkdir(exist_ok=True);runtime=private(directory)
 runtime.update(BTS_HOST_EXECUTABLE=str(out/'bin/mapping-artist'),BTS_BLEND_MATERIAL_DEMO='1',BTS_AE_PERSISTENCE_DEMO='1',BTS_VERIFY_ASYNC_SAVE='1',BTS_BLEND_MATERIAL_SOURCE=str(fixture),BTS_NATIVE_MAPPING_PROJECT=str(project),BTS_NATIVE_MAPPING_DESCRIPTOR=str(out/'artist/edited-descriptor.json'),BTS_TEMPLATE_DIR=str(directory/'templates'),BTS_ENVIRONMENT=env.get('BTS_ENVIRONMENT',str(bts/'build/environment-cache/TT_environmentLights_v001.hdr')))
 if cold:
  fixture.rename(fixture.with_suffix('.blend.hidden'));runtime['BTS_NATIVE_MAPPING_REOPEN']='1';assert not any(Path(runtime['BTS_RESOURCE_CACHE']).iterdir())
 try:
  with(directory/'run.log').open('w')as log:subprocess.run(['nice','-n','19','bash',str(repo/'scripts/host/run-host.sh'),'--no-splash','--workspace','--gui-fix-demo',str(directory),'--gui-fix-mesh',str(repo/'native/tests/fixtures/single_tile.obj'),'--gui-fix-resolution','128','--autosave-seconds','3600','--library-dir',str(Path(runtime['XDG_DATA_HOME'])/'library')],cwd=repo,env=runtime,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=300)
 finally:
  if cold:fixture.with_suffix('.blend.hidden').rename(fixture)
 text=(directory/'run.log').read_text();assert'BTS_SHUTDOWN clean'in text and'BTS_NATIVE_MAPPING_ARTIST PASS'in text
 assert not any(word in text for word in('Not freed memory blocks','BTS_GPU_STACK_FALLBACK','BTS_GPU_AUTHORING_CPU_READ','BTS_DRAW_FAILED','BTS_HOST_FAILED','Shader compilation failed'))
 proofs['cold_open'if cold else'artist']=json.loads((directory/'native-mapping-artist.json').read_text())
artist=proofs['artist'];cold=proofs['cold_open']
assert len(artist['captures'])==(6 if a.upstream_mapping else 4) and artist['one_gesture']and artist['zero_shader_recompiles']and artist['cpu_recipe_materializations']==0 and artist['async_save']
if a.upstream_mapping:assert artist['unused_mapping_rejected_atomically']
assert [capture['native_descriptor']['version']for capture in artist['captures']]==([2,3,2,3,2,3]if a.upstream_mapping else[2,3,2,3])
assert cold['cold_open']and len(cold['captures'])==1 and cold['captures'][0]['native_descriptor']==artist['captures'][-1]['native_descriptor']
assert cold['captures'][0]['material_gpu_samples'][0]==artist['captures'][0]['material_gpu_samples'][0]
assert max(capture['gpu_error']for capture in artist['captures']+cold['captures'])<2e-6
assert hashlib.sha256(fixture.read_bytes()).hexdigest()==source_sha
(out/'verified.json').write_text(json.dumps({'source_sha256':source_sha,'source_unchanged':True,'panel':panel,'surface_mapping':surface['linked_mapping'],'actual_source_groups':actual,'actual_source_sha256':source_hashes,**proofs},indent=2)+'\n')
print('Native linked Mapping: CPU + exact identity/affine GPU + actual artist Undo/Redo + cold packed Open PASS')
