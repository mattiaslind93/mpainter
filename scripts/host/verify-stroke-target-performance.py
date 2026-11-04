"""Qt widget paint/mask timings on two real imported UDIM planes; no desktop input."""
from parity_test_environment import private_display
import argparse,json,math,os,re,subprocess
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);p.add_argument('--resolution',type=int,default=2048,choices=[2048,4096]);p.add_argument('--existing',action='store_true');p.add_argument('--private-gui',action='store_true');p.add_argument('--require-goals',action='store_true');p.add_argument('--smudge',action='store_true');p.add_argument('--project',type=Path);a=p.parse_args();a.output=a.output.resolve();a.output.mkdir(parents=True,exist_ok=True);root=Path(__file__).resolve().parents[2]
env=dict(os.environ,LANG='sv_SE.UTF-8',LC_ALL='sv_SE.UTF-8',QT_QPA_PLATFORM='offscreen',BTS_HEADLESS_WORKSPACE='1',DISPLAY=private_display(),XDG_RUNTIME_DIR='/run/user/1000',BTS_STROKE_TARGET_PROFILE='1',BTS_PERFORMANCE_PROFILE='1',BTS_TEMPLATE_DIR=str(a.output/'templates'))
for key,folder in [('XDG_CONFIG_HOME','config'),('XDG_DATA_HOME','data'),('XDG_CACHE_HOME','cache'),('TMPDIR','tmp'),('BTS_RECOVERY_DIR','recovery'),('BTS_RESOURCE_CACHE','resources'),('BTS_SESSION_LOG_DIR','logs')]:
 (a.output/folder).mkdir(exist_ok=True);env[key]=str(a.output/folder)
if a.smudge:env['BTS_STROKE_TARGET_SMUDGE']='1'
args=['--workspace','--gui-fix-demo',str(a.output),'--gui-fix-mesh',str(root/'fixtures/profile_two_tiles.obj'),'--gui-fix-resolution',str(a.resolution),'--autosave-seconds','3600','--library-dir',str(a.output/'library')]
if a.project:
 project=a.project.resolve();build=root.parent/'build'
 assert project.is_file()and any(parent.exists()and os.path.samefile(parent,build)for parent in (project.parent,*project.parents)),'Only a private project copy under BTS build'
 env['BTS_STROKE_PROFILE_PROJECT']='1';env['BTS_SURFACE_CACHE_VERIFY']='1';args+=['--open-document',str(project)]
if not a.existing:
 with (a.output/'run.log').open('w')as log:
  if a.private_gui:
   from private_gpu_gui import private_gpu_gui
   with private_gpu_gui(a.output,env,root)as(prefix,private_env):subprocess.run(prefix+[str(root.parent/'build/host/bin/bts-host')]+args,env=private_env,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=720)
  else:subprocess.run(['bash',str(root/'scripts/host/run-host.sh')]+args,env=env,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=720)
d=json.loads((a.output/'stroke-target-performance.json').read_text());assert len(d['cases'])==8
phases=d['phases'];result={'resolution':a.resolution,'project_copy':str(a.project)if a.project else None,'dab_cpu_budget_ms':.35 if a.project else .3,'platform':d['platform'],'presentation':'owned Xvfb xcb/QOpenGLWidget through NVIDIA EGL/VirtualGL; transport overhead included'if d['platform']=='xcb'else'private offscreen Qt widget; same stroke callbacks; desktop platform delivery not injected','cases':[]}
def stats(values):
 v=sorted(values);return {'count':len(v),'p50':v[len(v)//2]if v else 0,'p95':v[max(0,math.ceil(.95*len(v))-1)]if v else 0,'max':max(v,default=0)}
for c in d['cases']:
 assert c['before']['profile_tiles']==[1001,1002]and c['after']['profile_depth']==1
 assert c['before']['profile_mask']==c['mask']and c['after']['cpu_materializations']==c['before']['cpu_materializations']
 assert c['before'].get('profile_smudge',False)==a.smudge and c['after'].get('profile_smudge',False)==a.smudge
 if a.smudge or a.project:
  assert c['before']['profile_filter_count']>=1
  if c['mask']:assert c['before']['profile_mask_filter_count']>=1
 assert c['after']['undo_count']==c['before']['undo_count']+3
 rows=[r for r in phases if r['flow']==c['label']]
 summary={k:c[k]for k in ['label','mask','scene','tile','max_ping_ms','transfers']}
 for name in ['stroke_geometry_dab_cpu','stroke_raster_dab_cpu','BTS_STROKE_MS','draw','gpu_render','upload_staging_images']:
  selected=[r for r in rows if r['phase']==name];summary[name]={'wall_ms':stats([r['wall_ms']for r in selected]),'cpu_ms':stats([r['cpu_ms']for r in selected])}
 summary['handler_wall_ms']=stats([r['wall_ms']for r in c['calls']]);summary['handler_cpu_ms']=stats([r['cpu_ms']for r in c['calls']]);summary['cpu_materializations']=0
 assert c['transfers']['finish']['calls']==0,(c['label'],c['transfers'])
 # The counter also sees fence-ready 48x48 RGBA thumbnail SSBOs and
 # 32-byte status packets. Count those separately; faster processing may
 # complete both layer and mask thumbnails in one stroke. Full-image reads
 # cannot satisfy this exact packet accounting or the CPU-time budget.
 read=c['transfers']['readback'];thumbnail_bytes=48*48*16
 packets,remainder=divmod(read['bytes']-32*read['calls'],thumbnail_bytes-32)
 assert remainder==0 and 0<=packets<=read['calls'],(c['label'],read)
 assert read['cpu_wall_ms']<5,(c['label'],read)
 summary['thumbnail_packets']=packets;summary['status_packets']=read['calls']-packets
 summary['readback_note']='Fence-ready thumbnail/status packets included; no full-image readback or GPU_finish'
 assert summary['stroke_raster_dab_cpu']['cpu_ms']['count']>0
 result['cases'].append(summary)
log=(a.output/'run.log').read_text();assert 'BTS_BRUSH_CPU_FALLBACK'not in log and 'BTS_GPU_AUTHORING_CPU_READ'not in log
assert 'BTS_NATIVE_GPU_ORACLE'in log and 'BTS_SHUTDOWN clean'in log
result['surface_cache_builds']=log.count('BTS_SURFACE_CACHE_BUILD')
result['uncached_geometry_oracles']=log.count('BTS_SURFACE_CACHE_ORACLE')
if a.project:assert result['surface_cache_builds']>=1 and result['uncached_geometry_oracles']>=8
(a.output/'verified.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))

if a.require_goals:
 for c in result['cases']:
  assert c['stroke_geometry_dab_cpu']['cpu_ms']['p95']+c['stroke_raster_dab_cpu']['cpu_ms']['p95']<=(.35 if a.project else .3),(c['label'],'dab CPU budget (dense project .35, fixture .3)',c)
  assert c['handler_wall_ms']['max']<=50,(c['label'],'event budget',c)
  assert c['draw']['wall_ms']['p95']<=33,(c['label'],'frame budget',c)
  assert c['max_ping_ms']<100,(c['label'],'event loop budget',c)
