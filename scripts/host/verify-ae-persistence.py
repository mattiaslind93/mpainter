#!/usr/bin/env python3
"""Exercise the real asynchronous workspace Save and autosave paths offscreen."""
import argparse,json,os,re,subprocess
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);p.add_argument('--mesh',type=Path);p.add_argument('--project',type=Path);p.add_argument('--record-only',action='store_true');p.add_argument('--paint',action='store_true');p.add_argument('--resolution',type=int,choices=(512,1024,2048,4096),default=4096);a=p.parse_args()
assert bool(a.mesh)!=bool(a.project),'Choose a synthetic mesh or an existing project copy'
root=Path(__file__).resolve().parents[2];out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
assert out.is_relative_to(Path(os.environ.get('BTS_ROOT','/home/mlind/bts'))/'build')
env=dict(os.environ,QT_QPA_PLATFORM='offscreen',BTS_HEADLESS_WORKSPACE='1',BTS_AE_PERSISTENCE_DEMO='1',BTS_VERIFY_ASYNC_SAVE='1',BTS_PERFORMANCE_PROFILE='1',LANG='sv_SE.UTF-8',LC_ALL='sv_SE.UTF-8',BTS_AE_RESOLUTION=str(a.resolution))
if a.paint:env['BTS_AE_PAINT']='1'
if a.project:
 assert a.project.resolve().is_relative_to(Path(os.environ.get('BTS_ROOT','/home/mlind/bts'))/'build'),'Existing-project benchmarks must use a copy under build/'
 env['BTS_AE_PROJECT']=str(a.project.resolve())
for key,folder in (('XDG_CONFIG_HOME','config'),('XDG_DATA_HOME','data'),('XDG_CACHE_HOME','cache'),('BTS_RECOVERY_DIR','recovery'),('BTS_SESSION_LOG_DIR','logs')):
 env[key]=str(out/folder);(out/folder).mkdir(exist_ok=True)
command=['bash',str(root/'scripts/host/run-host.sh'),'--workspace','--gui-fix-demo',str(out),'--gui-fix-mesh',str(a.mesh.resolve() if a.mesh else root/'fixtures/single_tile.obj'),'--gui-fix-resolution','4096','--autosave-seconds','2']
with (out/'run.log').open('w') as log:run=subprocess.run(command,env=env,stdout=log,stderr=subprocess.STDOUT,timeout=240)
log=(out/'run.log').read_text();proof=json.loads((out/'ae-persistence.json').read_text()) if (out/'ae-persistence.json').is_file() else {'error':'No proof written'}
proof['returncode']=run.returncode
proof['opengl_debug_errors']=log.count('BTS_GL_ERROR')
renderer=re.search(r'BTS_GPU_READY[^\n]*renderer=(.*?) qt=',log)
proof['renderer']=renderer[1] if renderer else 'unknown'
proof['performance_gate_enabled']=not a.record_only
if not a.record_only:assert 'llvmpipe' not in proof['renderer'].lower(),'Software runs are functional checks only'
assert 'BTS_SSBO_ASYNC copy shader failed' not in log, 'GPU storage snapshot shader rejected by driver'
proof['cpu_finalize_count']=log.count('BTS_FINALIZE_MS');proof['cpu_padding_count']=len(re.findall(r'phase=padding\b',log))
proof['ui_autosave_count']=len(re.findall(r'phase=autosave_checkpoint[^\n]*ui=1',log))
proof['gpu_authoring_counters']=re.findall(r'BTS_GPU_AUTHORING[^\n]*',log)
interaction_log=log.split('BTS_AE_INTERACTION_BEGIN',1)[-1].split('BTS_AE_INTERACTION_END',1)[0]
proof['freezes']=[line for line in interaction_log.splitlines() if 'FRYSVARNING' in line]
(out/'verified.json').write_text(json.dumps(proof,indent=2)+'\n')
assert run.returncode==0,(run.returncode,log[-4000:])
assert not proof['error'],proof['error']
assert proof['opengl_debug_errors']==0, 'OpenGL error during native save/render session'
if not a.project:assert proof['final']['uv_set_switch_pixels_checked'],'UV renderer must switch actual pixels between sets and restore them'
assert proof['final']['channel_controls_parent_stable'],'Paint/Fill selection must keep the shared control tree parent stable'
assert proof['final']['native_reopen'],'The asynchronous save must reopen in the native host'
assert proof['cpu_finalize_count']==proof['cpu_padding_count']==proof['ui_autosave_count']==0,proof
assert len(proof['final']['sets'])>=(1 if a.project else 2) and all(s['resolution']==a.resolution and s['tiles']>=2 and s['layers']>=5 for s in proof['final']['sets'])
if not a.record_only:
 assert proof['max_ping_ms']<50,proof['max_ping_ms']
 assert all(x.get('handler_ms',0)<50 for x in proof['operations']),proof['operations']
 assert not proof['freezes'],proof['freezes']
 assert next(x['wall_ms'] for x in proof['operations'] if x['operation']=='ae_manual_save_total')<3000
print(json.dumps({k:proof[k] for k in ('returncode','error','max_ping_ms','operations','cpu_finalize_count','cpu_padding_count','ui_autosave_count')},indent=2))
