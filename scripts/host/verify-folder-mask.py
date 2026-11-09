# SPDX-License-Identifier: GPL-3.0-or-later
"""Explicit private native BK proof. Never attaches a window to the desktop."""
from pathlib import Path
import argparse,hashlib,json,os,struct,subprocess,time,zlib
from private_gpu_gui import private_gpu_gui
def verify_proof(directory,phase):
 proof=json.loads((directory/'folder-mask.json').read_text());assert not proof['failure'] and proof['captures']
 if phase=='warm':
  stages={row['stage']for row in proof['checks']};assert {'passthrough-quarter','normal-quarter','postcommit-refresh-resource-retained','bitmap-file','cancelled-folder-load','folder-file-drop-autocreate','folder-texture-shelf-drop','cycles-mask-three-quarters','cycles-mask-undo-quarter','cycles-mask-redo-three-quarters','mask-export','group-export','mask-isolation-material-restored','reopened-final-stack','folder-paint-redone'}<=stages
  names=[row['stage']for row in proof['captures']];assert {'folder-mask-painted','saved','reopened','folder-mask-isolation','folder-blur','folder-sharpen','folder-warp','folder-native-generator','all-folder-mask-effects','flattened','flatten-undone','flatten-redone'}<=set(names)
  # These captures precede all explicit Flatten/Export oracle stages.
  captures=proof['captures'][:names.index('reopened')+1]
 else:
  assert {'source-cache-absent-bitmap','source-cache-absent-painted','source-cache-absent-final-stack'}<={row['stage']for row in proof['checks']};captures=proof['captures']
 keys=['cpu_materializations','cpu_layer_compositions'];baseline={key:captures[0][key]for key in keys};rows=[]
 for capture in captures:
  values={key:capture[key]for key in keys};deltas={key:values[key]-baseline[key]for key in keys}
  assert all(value==0 for value in deltas.values()),(capture['stage'],baseline,values,deltas)
  rows.append({'stage':capture['stage'],'values':values,'deltas':deltas})
 result={'phase':phase,'scope':'Interactive captures through reopened; later explicit Flatten/Export oracle stages excluded'if phase=='warm'else'Cold native Open captures','baseline':baseline,'captures':rows,'zero_interactive_deltas':True}
 (directory/'interactive-cpu-counters.json').write_text(json.dumps(result,indent=2)+'\n');return result
p=argparse.ArgumentParser();p.add_argument('--build-dir',type=Path,required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('--phase',choices=['warm','cold'],required=True);p.add_argument('--warm',type=Path);p.add_argument('--verify-core',action='store_true');p.add_argument('--existing',action='store_true');a=p.parse_args()
root=Path(__file__).resolve().parents[2];out=a.output.resolve()
if a.existing:
 receipt=json.loads((out/'receipt.json').read_text());assert receipt['actual_exit']==0 and receipt['clean_exit'] and receipt['phase']==a.phase
 print(json.dumps(verify_proof(out,a.phase),indent=2));raise SystemExit(0)
out.mkdir(parents=True,exist_ok=False)
assert Path('/run/.containerenv').exists()and os.getpriority(os.PRIO_PROCESS,0)==19,'Run inside bts-build with nice -n 19'
def sha(path):
 with path.open('rb')as file:return hashlib.file_digest(file,'sha256').hexdigest()
def chunk(kind,data):return struct.pack('>I',len(data))+kind+data+struct.pack('>I',zlib.crc32(kind+data)&0xffffffff)
if a.phase=='warm':
 fixtures=out/'fixtures';fixtures.mkdir();mesh=fixtures/'plane.obj';mesh.write_text('o BKPlane\nv -1 -1 0\nv 1 -1 0\nv 1 1 0\nv -1 1 0\nvt 0 0\nvt 1 0\nvt 1 1\nvt 0 1\nvn 0 0 1\nf 1/1/1 2/2/1 3/3/1\nf 1/1/1 3/3/1 4/4/1\n')
 bitmap=fixtures/'BK Region.png';rows=b''.join(b'\0'+b''.join(bytes((n,n,n,255))for n in [64]*8+[192]*8)for _ in range(16));bitmap.write_bytes(b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',16,16,8,6,0,0,0))+chunk(b'IDAT',zlib.compress(rows))+chunk(b'IEND',b''))
 fixture_info={'png_sha256':sha(bitmap),'literal_red_bytes':[64,192],'decoded_raw_values':[64/255,192/255],'geometry_sha256':sha(mesh)}
else:
 assert a.warm,'Cold needs an existing successful warm proof'
 warm=a.warm.resolve();prior=json.loads((warm/'receipt.json').read_text());assert prior['actual_exit']==0 and prior['clean_exit'] and prior['phase']=='warm';verify_proof(warm,'warm')
 fixtures=warm/'fixtures';mesh=fixtures/'plane.obj';bitmap=fixtures/'BK Region.png';assert fixtures.is_dir()
 held=fixtures.with_name('fixtures.held');assert not held.exists();fixtures.rename(held)
 cache=warm/'private/resources';assert cache.is_dir();held_cache=cache.with_name('resources.held');assert not held_cache.exists();cache.rename(held_cache)
 library=warm/'private/data';held_library=library.with_name('data.held');assert library.is_dir()and not held_library.exists();library.rename(held_library)
 assert not mesh.exists()and not bitmap.exists()and not cache.exists()and not library.exists()
 fixture_info={'source_directory_absent':True,'warm_resource_cache_absent':True,'warm_user_library_absent':True,'held_user_library':str(held_library),'held_fixture_directory':str(held),'held_resource_cache':str(held_cache),'warm_binary_sha256':prior['binary_sha256']}
base=out/'private';base.mkdir();env=os.environ.copy()
for key in ['DISPLAY','WAYLAND_DISPLAY','XAUTHORITY','LD_PRELOAD']:env.pop(key,None)
for key,folder in [('BLENDER_USER_CONFIG','blender-config'),('CUDA_CACHE_PATH','cuda-cache'),('__GL_SHADER_DISK_CACHE_PATH','nvidia-cache')]:
 (base/folder).mkdir();env[key]=str(base/folder)
env.update(BTS_BK_FOLDER_MASK='1',BTS_BK_BITMAP=str(bitmap),BTS_FLATTEN_VERIFY_OUTPUT='1',BTS_VERIFY_CYCLES_LIVE='1',BTS_ROOT=str(root.parent),BTS_ENVIRONMENT='/home/mlind/.config/blender/5.1/datafiles/studiolights/world/TT_environmentLights_v001.hdr')
if a.verify_core:env.update(BTS_VERIFY_GPU_STACK='1',BTS_RESOLUTION_LOG='1')
else:env.pop('BTS_VERIFY_GPU_STACK',None)
if a.phase=='cold':env.update(BTS_BK_COLD='1',BTS_BK_PROJECT_DIR=str(warm))
else:env.pop('BTS_BK_COLD',None);env.pop('BTS_BK_PROJECT_DIR',None)
binary=(a.build_dir/'bin/bts-host').resolve();before_sha=sha(binary);script_sha=sha(Path(__file__));start=time.monotonic()
with private_gpu_gui(base,env,root)as(prefix,child):
 assert not list(Path(child['BTS_RESOURCE_CACHE']).iterdir()),'Proof must start with an empty private resource cache'
 command=prefix+[str(binary),'--no-splash','--workspace','--gui-fix-demo',str(out),'--gui-fix-mesh',str(mesh),'--gui-fix-resolution','128']
 with(out/'run.log').open('w')as log:
  process=subprocess.Popen(command,env=child,stdout=log,stderr=subprocess.STDOUT);(out/'pid.json').write_text(json.dumps({'pid':process.pid,'command':command})+'\n')
  try:code=process.wait(timeout=900)
  except subprocess.TimeoutExpired:
   process.terminate()
   try:process.wait(timeout=15)
   except subprocess.TimeoutExpired:process.kill();process.wait()
   code=124
 log=(out/'run.log').read_text(errors='replace');receipt={'phase':a.phase,'actual_exit':code,'seconds':time.monotonic()-start,'pid':process.pid,'binary_sha256':before_sha,'binary_bytes':binary.stat().st_size,'binary_unchanged':sha(binary)==before_sha,'verifier_sha256':script_sha,'verifier_unchanged':sha(Path(__file__))==script_sha,'diagnostic_environment':{key:child.get(key)for key in ['BTS_VERIFY_GPU_STACK','BTS_RESOLUTION_LOG','BTS_BK_FOLDER_MASK','BTS_FLATTEN_VERIFY_OUTPUT','BTS_VERIFY_CYCLES_LIVE']},'command':command,'fixture':fixture_info,'core_requested':a.verify_core,'folder_core_marker':'BTS_GPU_FOLDER_COMPOSITION_VERIFIED'in log,'clean_exit':'BTS_SHUTDOWN clean'in log,'display':json.loads((base/'private-display.json').read_text())}
 (out/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
 print(json.dumps(receipt,indent=2),flush=True)
 if code:raise SystemExit(code)
 assert receipt['binary_unchanged'] and receipt['verifier_unchanged'] and receipt['clean_exit'],'Actual normal shutdown was not observed'
 if a.verify_core:assert receipt['folder_core_marker'],'Core GPU Folder checks did not execute'
 verify_proof(out,a.phase)
