# SPDX-License-Identifier: GPL-3.0-or-later
"""Private coherent actual-host Cycles live-image regression; no desktop input."""
import argparse, concurrent.futures, hashlib, json, os, re, shlex, subprocess, shutil, sys
import resource
from cycles_live_manifest import sha256, validate_manifest, fresh_runtime, freeze_admission, require_completed_attempt, require_displacement_attempt
resource.setrlimit(resource.RLIMIT_CORE,(0,0))
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--snapshot',type=Path,required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('--cpu-only',action='store_true');p.add_argument('--build-only',action='store_true');p.add_argument('--run-only',action='store_true');p.add_argument('--run-manifest',type=Path,help='Authoritative consolidated compile/refresh manifest (default: output/run-manifest.json)');p.add_argument('--udim',action='store_true',help='Known native WorldHeight mask, signed/HDR Color and two2048 UDIMs');p.add_argument('--sanitize-bridge',action='store_true',help='ASan/UBSan for bridge and cache-test TUs; reused Blender remains uninstrumented');p.add_argument('--jobs',type=int,choices=(1,2),default=2);p.add_argument('--integrated-snapshot',action='store_true',help='Require full rebuilt native/fork attestation; compile test drivers only, never replay patches');p.add_argument('--controlled-profile',action='store_true',help='Explicit test-only fixed-sampling completion/no-op diagnostic; requires integrated UDIM mode');p.add_argument('--burst-profile',action='store_true',help='Separate real held-picker burst; requires integrated UDIM and both immutable parents');p.add_argument('--burst-previous-attempt',type=Path,help='Retained first three-second inconclusive attempt, never rerun');p.add_argument('--controlled-parent-manifest',type=Path,help='Prior final reviewed controlled diagnostic build manifest; provenance only');p.add_argument('--displacement-profile',action='store_true',help='Independent controlled128 role16 displaced-geometry actor; integrated snapshot only');p.add_argument('--test-parent-manifest',type=Path,help='Prior immutable default integrated build manifest for the controlled test overlay');a=p.parse_args()
repo=Path(__file__).resolve().parents[2];bts=repo.parent;snapshot=a.snapshot.resolve();out=a.output.resolve()
if not a.integrated_snapshot and (repo/'native/patches/0037-cycles-hosted-displacement-image-refresh.patch').is_file():raise SystemExit('Reviewed0037 requires --integrated-snapshot; selective patch replay is not full integration')
if a.displacement_profile:
 from cycles_integrated_snapshot import require_displacement_options
 require_displacement_options(a)
assert out.is_relative_to(bts/'build')and not out.is_relative_to(bts/'build/host')and not out.is_relative_to(snapshot)
out.mkdir(parents=True,exist_ok=True);(out/'objects').mkdir(exist_ok=True);(out/'bin').mkdir(exist_ok=True)
env=dict(os.environ,CCACHE_DIR=str(bts/'build/ccache'))
def digest(path):return sha256(path)
def call(args,label):
 with(out/(label+'.log')).open('w')as log:subprocess.run(['nice','-n','19',*map(str,args)],cwd=snapshot,env=env,stdout=log,stderr=subprocess.STDOUT,check=True)
if sum(map(bool,(a.controlled_profile,a.burst_profile,a.displacement_profile)))>1:raise SystemExit('Choose exactly one diagnostic profile')
if (a.controlled_profile or a.burst_profile) and (not a.integrated_snapshot or not a.udim or a.test_parent_manifest is None):raise SystemExit('Controlled profile requires integrated UDIM and an explicit prior default parent manifest')
if not (a.controlled_profile or a.burst_profile) and a.test_parent_manifest is not None:raise SystemExit('Test parent manifest requires an explicit diagnostic profile')
if a.burst_profile != (a.burst_previous_attempt is not None):raise SystemExit('Six-second burst requires its original three-second attempt evidence')
if a.burst_profile != (a.controlled_parent_manifest is not None):raise SystemExit('Burst requires exactly one explicit controlled parent manifest')
if a.integrated_snapshot and a.sanitize_bridge:raise SystemExit('Integrated mode reuses production objects; selective bridge rebuilding is unsupported')
if not a.run_only and a.integrated_snapshot:
 from cycles_integrated_snapshot import build_drivers
 build_drivers(snapshot,repo,out,a.udim,env,a.controlled_profile,a.test_parent_manifest,a.burst_profile,a.controlled_parent_manifest,a.burst_previous_attempt,a.displacement_profile)
if not a.run_only and not a.integrated_snapshot:
 commands=json.loads((snapshot/'host-commands.json').read_text());native=[]
 frozen_source=out/'source';source_hashes={}
 for folder in ('host','tests','spsm','sbsar_metadata'):
  shutil.copytree(repo/'native'/folder,frozen_source/'native'/folder,dirs_exist_ok=True)
 for path in (frozen_source/'native').rglob('*'):
  if path.is_file()and path.suffix in ('.cc','.hh','.inc'):source_hashes[str(path.relative_to(frozen_source))]=digest(path)
 overlay=out/'blender-overlay';overlay.mkdir(exist_ok=True);pinned=bts/'blender';fork_sources={};fork_manifest={}
 for patch in ('0035-hosted-live-image-cache.patch','0036-cycles-live-image-reload.patch'):
  patchfile=repo/'native/patches'/patch
  paths=re.findall(r'^--- a/(.*)$',patchfile.read_text(),re.M)
  for rel in paths:
   source=pinned/rel;dest=overlay/rel;dest.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(source,dest)
   fork_manifest[rel]={'base_sha256':digest(source)}
  call(['patch','--fuzz=0','-d',overlay,'-p1','-i',patchfile],patch+'.apply')
 for rel,metadata in fork_manifest.items():metadata['overlay_sha256']=digest(overlay/rel)
 for cmd in commands:
  if ' -c 'not in cmd:continue
  if not any('/'+rel in cmd for rel in fork_manifest if rel.endswith(('.cc','.cpp'))):continue
  args=shlex.split(cmd);source=args[args.index('-c')+1]
  rel=next((rel for rel in fork_manifest if source.endswith('/'+rel)),None)
  if rel:fork_sources[args[args.index('-o')+1]]=(args,overlay/rel)

 for cmd in commands:
  if ' -c 'in cmd and '/native/'in cmd:
   args=shlex.split(cmd)
   if '-c'in args and '/native/'in args[args.index('-c')+1]:native.append(args)
 source_root=Path(native[0][native[0].index('-c')+1].split('/native/')[0]);baseline=json.loads((snapshot/'snapshot.json').read_text())['source_head']
 changes=set(subprocess.check_output(['git','diff','--name-only',baseline,'--','native/host'],cwd=repo,text=True).splitlines());headers={Path(v).name for v in changes if v.endswith('.hh')};deps={};key=None;generated={}
 generated_root=out/'generated'
 build_root=bts/'build/host'
 for line in(snapshot/'native-deps.txt').read_text().splitlines():
  if ': #deps 'in line:key=line.split(': #deps ')[0];deps[key]=set();assert '(VALID)'in line
  elif key and line.startswith('    '):
   dep=Path(line.strip());deps[key].add(dep.name);dep=(dep if dep.is_absolute()else build_root/dep).resolve()
   if dep.is_relative_to(build_root)and dep.is_file():
    dest=generated_root/dep.relative_to(build_root);dest.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(dep,dest);generated[str(dep.relative_to(build_root))]=digest(dest)
 assert len(deps)==len(native),'Incomplete frozen Ninja dependency audit'
 # Fork TUs have no host ABI changes. Freeze their generated include files too;
 # read Ninja dependency metadata only, never rebuild or modify artist inputs.
 fork_deps=subprocess.check_output(['ninja-build','-C',str(build_root),'-t','deps',*fork_sources],text=True)
 (out/'fork-deps.txt').write_text(fork_deps)
 for line in fork_deps.splitlines():
  if not line.startswith('    '):continue
  dep=Path(line.strip());dep=(dep if dep.is_absolute()else build_root/dep).resolve()
  if dep.is_relative_to(build_root)and dep.is_file():
   dest=generated_root/dep.relative_to(build_root);dest.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(dep,dest);generated[str(dep.relative_to(build_root))]=digest(dest)

 objects={};recompiled=[]
 def compile(args,source,label):
  args=[v.replace(str(source_root/'native'),str(frozen_source/'native')).replace(str(build_root/'source'),str(generated_root/'source')).replace(str(build_root/'intern'),str(generated_root/'intern'))for v in args];source=frozen_source/source.relative_to(repo)if source.is_relative_to(repo/'native')else source;obj=out/'objects'/(label+'.o');args[args.index('-o')+1]=str(obj);args[args.index('-c')+1]=str(source)
  for flag,val in(('-MF',str(obj)+'.d'),('-MT',str(obj))):
   if flag in args:args[args.index(flag)+1]=val
  args.insert(next(i for i,v in enumerate(args)if v.startswith('-')), '-I'+str(overlay/'source/blender/blenkernel'));args.insert(next(i for i,v in enumerate(args)if v.startswith('-')), '-I'+str(overlay/'intern/cycles'));args+=['-I'+str(frozen_source/'native/host'),'-g0']
  if a.sanitize_bridge and label in ('native-cycles-images','cycles-cache-cpu'):args+=['-O1','-fsanitize=address,undefined','-fno-omit-frame-pointer']
  call(args,label+'.compile');return str(obj)
 def unit(args):
  old=args[args.index('-o')+1];source=Path(args[args.index('-c')+1]);rel='native/'+str(source).split('/native/',1)[1]
  if rel in changes or headers&deps[old]:
   label=source.stem+'-'+hashlib.sha256(old.encode()).hexdigest()[:10];new=compile(args,repo/rel,label);return old,new,True
  return old,str((snapshot/old).resolve()),False
 with concurrent.futures.ThreadPoolExecutor(max_workers=a.jobs)as pool:
  for old,new,changed in pool.map(unit,native):objects[old]=new;recompiled.extend([old]if changed else[])
 template=next(v for v in native if v[v.index('-c')+1].endswith('/main.cc'))
 driver_source=repo/'native/tests'/('cycles_live_udim_artist_main.cc'if a.udim else'cycles_live_artist_test.cc');driver=compile(template,driver_source,'cycles-live-artist')
 udim_case=compile(template,repo/'native/tests/cycles_live_udim_artist_test.cc','cycles-live-udim-case')if a.udim else None
 extra_template=next(v for v in native if v[v.index('-c')+1].endswith('/ordinary_color_source_test.cc'))
 extra=compile(extra_template,repo/'native/tests/spsm_bitmap_color_test.cc','source-color-cpu')
 bridge=compile(template,repo/'native/host/blender/NativeCyclesImages.cc','native-cycles-images')
 cache_test=compile(template,repo/'native/tests/cycles_image_cache_test.cc','cycles-cache-cpu')
 fork_objects={}
 for old,(args,source)in fork_sources.items():fork_objects[old]=compile(args,source,'fork-'+source.parent.name+'-'+source.name)
 archives={}
 for old,obj in fork_objects.items():
  cmd=next(c for c in commands if ' qc 'in c and old in c)
  ar=shlex.split(cmd);pos=next(i for i,v in enumerate(ar)if Path(v).name in('ar','gcc-ar','llvm-ar'));ar=ar[pos:];name=ar[2];dest=out/Path(name).name
  if name not in archives:shutil.copyfile(snapshot/name,dest);archives[name]=str(dest)
  member=Path(old).name;members=subprocess.check_output([ar[0],'t',str(dest)],text=True).splitlines();assert members.count(member)==1,(name,member)
  replacement=out/'members'/Path(name).stem/member;replacement.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(obj,replacement)
  call([ar[0],'r',dest,replacement],Path(name).stem+'-'+member+'.replace');call(['ranlib',dest],Path(name).stem+'.index')
 for cmd in commands:
  if ' qc 'not in cmd or '/native/'not in cmd:continue
  args=shlex.split(cmd);index=next((i for i,v in enumerate(args)if Path(v).name in('ar','gcc-ar','llvm-ar')),None)
  if index is None:continue
  args=args[index:];args=args[:args.index('&&')]if '&&'in args else args;old=args[2];members=args[3:]
  if not any(v in recompiled for v in members):continue
  dest=out/Path(old).name
  if dest.exists():dest.unlink()
  copied=[]
  for v in members:
   path=Path(objects.get(v,v));path=path if path.is_absolute()else snapshot/path
   if not path.is_file():
    path=out/'objects'/(Path(old).stem+'-'+Path(v).name)
    with path.open('wb')as f:subprocess.run(['nice','-n','19',args[0],'p',str(snapshot/old),Path(v).name],stdout=f,check=True)
   copied.append(str(path))
  call([args[0],'qc',dest,*copied],Path(old).stem+'.archive');call(['ranlib',dest],Path(old).stem+'.index');archives[old]=str(dest)
 link=shlex.split(commands[-1]);link=link[2:]if link[:2]==[':','&&']else link;link=link[:link.index('&&')]if '&&'in link else link
 args=[];inputs=[]
 for v in link:
  if v.endswith('/main.cc.o'):v=driver
  else:v=archives.get(v,objects.get(v,v))
  if v.endswith(('.o','.a','.so'))and not v.startswith('-'):
   path=Path(v);path=path if path.is_absolute()else snapshot/path;assert path.is_file(),path;assert not path.resolve().is_relative_to(bts/'build/host');v=str(path)
  if v.startswith('-Wl,--dependency-file='):v='-Wl,--dependency-file='+str(out/'link.d')
  v=v.replace(str(bts/'build/host/bin/lib'),str(snapshot/'bin/lib'));args.append(v)
 args.insert(next(i for i,v in enumerate(args)if v.endswith('.a')),extra)
 args.insert(next(i for i,v in enumerate(args)if v.endswith('.a')),bridge)
 if udim_case:args.insert(next(i for i,v in enumerate(args)if v.endswith('.a')),udim_case)
 if a.sanitize_bridge:args+=['-fsanitize=address,undefined']
 hashes={}
 for value in args:
  if value.endswith(('.o','.a','.so'))and not value.startswith('-'):
   if value not in hashes:hashes[value]=digest(value)
   inputs.append({'path':value,'sha256':hashes[value]})
 args[args.index('-o')+1]=str(out/'bin/cycles-live-artist');call(args,'link')
 cpu_args=[cache_test if v==driver else v for v in args];cpu_args[cpu_args.index('-o')+1]=str(out/'bin/cycles-cache-cpu');call(cpu_args,'cpu-link')
 for resource in(bts/'build/host/bin').iterdir():
  if resource.is_dir()and resource.name[0].isdigit()and not(out/'bin'/resource.name).exists():(out/'bin'/resource.name).symlink_to(resource)
 (out/'build.json').write_text(json.dumps({'source_sha256':source_hashes,'generated_header_sha256':generated,'baseline':baseline,'head':subprocess.check_output(['git','rev-parse','HEAD'],cwd=repo,text=True).strip(),'commands_sha256':digest(snapshot/'host-commands.json'),'dependencies_sha256':digest(snapshot/'native-deps.txt'),'recompiled':recompiled,'inputs':inputs,'production_ndebug_preserved':True,'driver_sha256':digest(driver_source),'artist_binary_sha256':digest(out/'bin/cycles-live-artist'),'cpu_test_object_sha256':digest(cache_test),'cpu_binary_sha256':digest(out/'bin/cycles-cache-cpu'),'sanitized_bridge_only':a.sanitize_bridge,'fork':fork_manifest,'fork_objects':{old:{'object':obj,'sha256':digest(obj)}for old,obj in fork_objects.items()}},indent=2))
 (out/'run-manifest.json').write_text(json.dumps({'schema':1,'mode':'udim'if a.udim else'128','sanitized_bridge_only':a.sanitize_bridge,'source_sha256':source_hashes,'patch_sha256':{str(path.relative_to(repo)):digest(path)for path in(repo/'native/patches').glob('003[56]-*.patch')},'parents':[{'path':str(out/'build.json'),'sha256':digest(out/'build.json')}],'binaries':{str((out/'bin'/name).resolve()):digest(out/'bin'/name)for name in('cycles-live-artist','cycles-cache-cpu')}},indent=2))
if a.build_only:raise SystemExit(0)
run_manifest=a.run_manifest.resolve()if a.run_manifest else out/'run-manifest.json'
binary=out/'bin'/('cycles-cache-cpu'if a.cpu_only else'cycles-live-artist')
profile='live-displacement-controlled-v1'if a.displacement_profile else'held-picker-burst-6s-v1'if a.burst_profile else'controlled-fixed-sampling-v1'if a.controlled_profile else'default'
admitted=validate_manifest(run_manifest,repo,binary,'displacement'if a.displacement_profile else'udim'if a.udim else'128',a.sanitize_bridge,profile)
if a.integrated_snapshot:
 from cycles_integrated_snapshot import validate as validate_integrated, validate_test_headers, validate_overlay_copy, validate_runner_sources, controlled_parent
 production=validate_integrated(snapshot,repo,admitted.get("test_overlay",{}),profile)
 if a.controlled_profile or a.burst_profile or a.displacement_profile:
  validate_overlay_copy(out,production,admitted["test_overlay"],a.burst_profile,a.displacement_profile)
 if a.controlled_profile or a.burst_profile:
  recorded_parent=json.loads((out/'build.json').read_text()).get('default_parent',{})
  if Path(recorded_parent.get('path','')).resolve()!=a.test_parent_manifest.resolve():raise ValueError('Controlled diagnostic parent does not match explicit request')
 validate_test_headers(snapshot,out,json.loads((out/'test-headers.json').read_text()))
 if a.burst_profile:
  from cycles_burst_snapshot import validate_controlled_parent, validate_previous_attempt
  if json.loads((out/'build.json').read_text()).get('previous_attempt_evidence')!=validate_previous_attempt(a.burst_previous_attempt):raise ValueError('Original three-second attempt differs from admitted parent')
  parent=controlled_parent(a.test_parent_manifest,production,snapshot)[0]
  expected=validate_controlled_parent(a.controlled_parent_manifest,production,snapshot,parent)
  if json.loads((out/'build.json').read_text()).get('controlled_parent')!=expected:raise ValueError('Burst controlled parent differs from explicit request')
  validate_runner_sources(repo,json.loads((out/'build.json').read_text()).get('runner_source_sha256',{}),True)
 if a.displacement_profile:
  validate_runner_sources(repo,json.loads((out/'build.json').read_text()).get('runner_source_sha256',{}),displacement=True)
 if Path(admitted.get('integrated_snapshot','')).resolve()!=snapshot:raise ValueError('Run manifest is not bound to this integrated snapshot')
elif admitted.get('integrated_snapshot'):raise ValueError('Integrated run manifest requires explicit --integrated-snapshot')
runtime=fresh_runtime(out)
binary=freeze_admission(run_manifest,binary,runtime)
(runtime/'admission.json').write_text(json.dumps({'manifest':str(run_manifest),'manifest_sha256':digest(run_manifest),'binary':str(binary),'binary_sha256':digest(binary)},indent=2))
for key,folder in(('XDG_CONFIG_HOME','config'),('XDG_DATA_HOME','data'),('XDG_CACHE_HOME','cache'),('XDG_STATE_HOME','state'),('BTS_RECOVERY_DIR','recovery'),('BTS_RESOURCE_CACHE','resources'),('BTS_SESSION_LOG_DIR','logs'),('TMPDIR','tmp')):
 (runtime/folder).mkdir(exist_ok=True);env[key]=str(runtime/folder)
for folder in ('cuda','nvidia'):(runtime/'cache'/folder).mkdir(exist_ok=True)
env.update(CUDA_CACHE_PATH=str(runtime/'cache/cuda'),__GL_SHADER_DISK_CACHE_PATH=str(runtime/'cache/nvidia'))
env.update(BTS_VERIFY_CYCLES_LIVE='1',BTS_BA_EXPECT_FIXED='1',QT_QPA_PLATFORM='offscreen',BTS_HEADLESS_WORKSPACE='1',BTS_BLEND_MATERIAL_DEMO='1',BTS_HOST_EXECUTABLE=str(binary),QT_PLUGIN_PATH=str(bts/'qt/6.8.3/plugins'),LD_LIBRARY_PATH=str(bts/'qt/6.8.3/lib')+':'+str(snapshot/'bin/lib'),BTS_ENVIRONMENT=str(bts/'build/environment-cache/TT_environmentLights_v001.hdr'))
if a.burst_profile:env.update(BTS_PROPERTY_PERFORMANCE='1',BTS_PERFORMANCE_PROFILE='1')
if a.controlled_profile:env['BTS_BA_CONTROLLED_PROFILE']='1'
else:env.pop('BTS_BA_CONTROLLED_PROFILE',None)
env.pop('BTS_TEST_CONTINUOUS_FRAMES',None);env.pop('BTS_BA_TRACE_ONLY',None)
if a.sanitize_bridge:env.update(ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1',LSAN_OPTIONS='suppressions='+str(repo/'native/tests/spsm_bitmap_texture_lsan.supp'))
if a.cpu_only:
 with(runtime/'cpu-run.log').open('w')as log:subprocess.run(['nice','-n','19',str(binary)],env=env,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=90)
 raise SystemExit(0)
with(runtime/'run.log').open('w')as log:subprocess.run(['nice','-n','19','bash',str(repo/'scripts/host/run-host.sh'),'--workspace','--no-splash','--gui-fix-demo',str(runtime),'--gui-fix-mesh',str(repo/'native/tests/fixtures'/('two_tile_udim.obj'if a.udim else'single_tile.obj')),'--gui-fix-resolution','2048'if a.udim else'128','--library-dir',str(runtime/'shelf'),'--recovery-dir',str(runtime/'recovery')],cwd=repo,env=env,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=420)
if a.burst_profile:
 validate_runner_sources(repo,json.loads((out/'build.json').read_text())['runner_source_sha256'],True)
 # Invoke the admitted classifier by its hashed path; never silently reuse an
 # imported stale policy or classify a different default/controlled report.
 subprocess.run(['nice','-n','19',sys.executable,str(repo/'scripts/host/check-cycles-live-burst.py'),str(runtime)],check=True)
 validate_runner_sources(repo,json.loads((out/'build.json').read_text())['runner_source_sha256'],True)
elif a.displacement_profile:
 validate_manifest(run_manifest,repo,out/'bin/cycles-live-artist','displacement',False,profile)
 production=validate_integrated(snapshot,repo,admitted['test_overlay'],profile)
 validate_overlay_copy(out,production,admitted['test_overlay'],displacement=True)
 validate_runner_sources(repo,json.loads((out/'build.json').read_text())['runner_source_sha256'],displacement=True)
 require_displacement_attempt(runtime)
else:require_completed_attempt(runtime,a.udim,a.controlled_profile)
print('Actual Cycles artist evidence:',runtime/('verified.json'if a.displacement_profile else'cycles-live-burst.json'if a.burst_profile else'cycles-udim.json'if a.udim else'cycles-live.json'))
