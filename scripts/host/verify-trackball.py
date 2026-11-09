"""Prepared AX acceptance: actual pointer checkbox + live Alt drags + fresh prefs cold.
Run only after root approves the frozen host and reserves the private RTX lane.
DPR2/imported-camera math remain focused CPU evidence unless separately run here.
Explicit captures/picking are diagnostics after gestures, never performance results.
"""
import argparse,configparser,hashlib,json,math,os,pathlib,signal,subprocess,time
from private_gpu_gui import private_gpu_gui
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--output',type=pathlib.Path,required=True)
p.add_argument('--binary',type=pathlib.Path,required=True)
p.add_argument('--expected-binary-sha256',required=True)
p.add_argument('--environment',type=pathlib.Path,required=True)
a=p.parse_args()
source=pathlib.Path(__file__).resolve().parents[2];base=source.parent;out=a.output.resolve();binary=a.binary.resolve();environment=a.environment.resolve()
assert pathlib.Path('/run/.containerenv').exists() and os.getpriority(os.PRIO_PROCESS,0)==19
assert out.is_relative_to(base/'build') and not out.exists()
assert binary.is_relative_to(base/'build') and binary.is_file()
def sha(path):
 with pathlib.Path(path).open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
assert sha(binary)==a.expected_binary_sha256
assert sha(environment)=='48505ffc80c0ca026775c77621b5a85a4c44b0fb7819ddadfa9e7bc6f5732031'
source_hashes={str(p.relative_to(source)):sha(p) for p in sorted((source/'native/host').rglob('*')) if p.is_file()}
for name in ['scripts/host/verify-trackball.py','scripts/host/private_gpu_gui.py','native/tests/fixtures/single_tile.obj']:
 source_hashes[name]=sha(source/name)
def guard():
 assert sha(binary)==a.expected_binary_sha256, 'Frozen ELF changed'
 assert all(sha(source/name)==value for name,value in source_hashes.items()), 'Frozen source changed'
 assert int(subprocess.check_output(['du','-s','-B1',str(base/'build')],text=True).split()[0])<49_800_000_000, 'Shared build budget49.8GB'
def no_other_host():
 for path in pathlib.Path('/proc').iterdir():
  if not path.name.isdecimal():continue
  try:name=(path/'exe').readlink().name
  except OSError:continue
  assert name not in ('bts-host','blender','bts-fragment-diagnostic','native-height-images'), 'Another native application owns the GPU lane: '+name
out.mkdir();preferences=out/'config';preferences.mkdir()
result={'complete':False,'actual_exit':None,'binary_sha256':a.expected_binary_sha256,'source_hashes':source_hashes,'runs':[],
        'math_bound':2e-5,'picking_uv_bound':2e-4,'requested_sensitivity':1.5,'default_trackball':False,
        'scope':'Actual checkbox/Alt drags/roll/turntable/Shift/Scene+UV/picking/idle/cold private prefs. No latency, Blender axis-snap parity or imported-camera GUI claim; screenshots and picks are explicit postgesture diagnostics.'}
def save():(out/'receipt.json').write_text(json.dumps(result,indent=2)+'\n')
save();active=None
def rows(view):return [[view['scene_view_matrix'][j*4+i]for j in range(3)]for i in range(3)]
def error(a,b):return max(abs(a[i][j]-b[i][j])for i in range(3)for j in range(3))
def expected(before,offset,delta):
 x,y,w,h=next(p['rect']for p in before['panes']if p['view']=='3D');dpr=before['device_pixel_ratio']
 logical=[(x+w/2)/dpr+offset[0],(y+h/2)/dpr+offset[1]]
 def vector(point):
  # Explicit inclusive Blender rcti endpoints, independent of production helper.
  xmax=w-1;ymax=h-1;assert min(xmax,ymax)>0
  a=(math.floor(point[0]*dpr)-x-xmax//2)/(min(xmax,ymax)/2);b=(h-1-(math.floor(point[1]*dpr)-y)-ymax//2)/(min(xmax,ymax)/2);d=math.hypot(a,b);t=1.1/math.sqrt(2);return [a,b,math.sqrt(1.1**2-d*d)if d<t else t*t/d]
 a=vector(logical);b=vector([logical[0]+delta[0],logical[1]+delta[1]]);axis=[a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]];n=math.sqrt(sum(v*v for v in axis));angle=math.sqrt(sum((b[i]-a[i])**2 for i in range(3)))/(2*1.1)*math.pi*1.5;angle=(angle+math.pi)%(2*math.pi)-math.pi
 if n==0:return rows(before)
 x,y,z=[v/n for v in axis];c=math.cos(angle);s=math.sin(angle);t=1-c;rotation=[[t*x*x+c,t*x*y-s*z,t*x*z+s*y],[t*x*y+s*z,t*y*y+c,t*y*z-s*x],[t*x*z-s*y,t*y*z+s*x,t*z*z+c]];prior=rows(before);return [[sum(rotation[i][k]*prior[k][j]for k in range(3))for j in range(3)]for i in range(3)]
try:
 for reload in [False,True]:
  guard();no_other_host()
  run=out/('reload'if reload else 'initial');run.mkdir()
  env={k:v for k,v in os.environ.items() if not k.startswith(('BTS_','XDG_','BLENDER_','QT_')) and k not in ('DISPLAY','WAYLAND_DISPLAY','XAUTHORITY','LD_PRELOAD')}
  env.update(BTS_ROOT=str(base),BTS_ENVIRONMENT=str(environment),BTS_TRACKBALL_DEMO='1',BTS_TEST_CONTINUOUS_FRAMES='1',BTS_SESSION_LOG_DIR=str(run/'logs'),QT_SCALE_FACTOR='1',PYTHONDONTWRITEBYTECODE='1')
  for key,folder in [('BLENDER_USER_CONFIG','blender'),('CUDA_CACHE_PATH','cuda'),('__GL_SHADER_DISK_CACHE_PATH','nvidia')]:
   (run/folder).mkdir();env[key]=str(run/folder)
  if reload:env['BTS_TRACKBALL_RELOAD']='1'
  record={'reload':reload,'complete':False,'actual_exit':None};result['runs'].append(record);save();started=time.monotonic()
  with private_gpu_gui(run,env,source)as(prefix,private):
   private['XDG_CONFIG_HOME']=str(preferences)
   command=prefix+['nice','-n','19',str(binary),'--workspace','--environment',str(environment),'--new-mesh',str(source/'native/tests/fixtures/single_tile.obj'),'--navigation-demo-dir',str(run),'--library-dir',str(run/'library'),'--recovery-dir',str(run/'recovery')]
   record['command']=command
   with(run/'run.log').open('x')as log:
    active=subprocess.Popen(command,cwd=source,env=private,stdout=log,stderr=subprocess.STDOUT,start_new_session=True);record['pid']=active.pid;save()
    while active.poll() is None:
     guard();assert time.monotonic()-started<600, 'Existing600s navigation deadline';time.sleep(2)
    record.update(actual_exit=active.returncode,seconds=time.monotonic()-started);active=None;save()
  log=(run/'run.log').read_text();record['clean_shutdown']='BTS_SHUTDOWN clean'in log;save()
  assert record['actual_exit']==0,record['actual_exit'];assert record['clean_shutdown']
  assert not any(marker in log for marker in ['BTS_HOST_FAILED','BTS_DRAW_FAILED','BTS_GL_ERROR','BTS_GPU_AUTHORING_CPU_READ','Not freed memory blocks'])
  proof=json.loads((run/'navigation.json').read_text());captures={c['stage']:c for c in proof['captures']};first=captures['initial'];assert first['view_state']['trackball']==reload
  for c in captures.values():
   assert c['document_matches_initial'];assert c['authoring_document']==first['authoring_document'];assert c['undo_count']==first['undo_count'] and c['redo_count']==first['redo_count'];assert c['document_revision']==first['document_revision'] and c['dirty']==first['dirty']
  for name in ['enabled','disabled','enabled-again','persist-enabled','layout-cancel','resize-cancel','focus-cancel','released-idle']:
   if name in captures:
    c=captures[name];assert error(rows(c['before_view_state']),rows(c['view_state']))<1e-6,(name,'toggle/layout changed view')
  if not reload:
   c=captures['trackball'];before=c['before_view_state'];r=next(p['rect']for p in before['panes']if p['view']=='3D');dpr=before['device_pixel_ratio'];assert error(rows(c['view_state']),expected(before,[-r[2]*.23/dpr,r[3]*.2/dpr],[90,-65]))<2e-5
   c=captures['noop-layout-drag'];before=c['before_view_state'];after=c['view_state']
   assert before['layout']==after['layout']=='3D only' and before['uv_first']==after['uv_first'] and before['panes']==after['panes']
   assert before['trackball']is True and after['trackball']is True
   assert error(rows(after),expected(before,[-50,60],[70,-35]))<2e-5, 'No-op F4 interrupted absolute trackball rotation'
   assert error(rows(after),rows(before))>1e-3, 'No-op F4 witness did not rotate'
   changed=captures['layout-cancel'];assert '/'in changed['before_view_state']['layout'] and '/'in changed['view_state']['layout'] and changed['before_view_state']['layout']!=changed['view_state']['layout'], 'Cancellation witness did not actually swap split layout'
   c=captures['trackball']
   v=c['view_state'];yaw=math.radians(v['scene_yaw']);pitch=math.radians(v['scene_pitch']);right=[math.cos(yaw),math.sin(yaw),0];up=[-math.sin(yaw)*math.sin(pitch),math.cos(yaw)*math.sin(pitch),math.cos(pitch)];actual=rows(v)[1];roll=math.atan2(sum(actual[i]*right[i]for i in range(3)),sum(actual[i]*up[i]for i in range(3)));assert abs(roll)>.01
   c=captures['turntable'];assert abs(c['view_state']['scene_yaw']-(c['before_view_state']['scene_yaw']-30*.35))<1e-4;assert abs(c['view_state']['scene_pitch']-max(-90,min(90,c['before_view_state']['scene_pitch']+20*.35)))<1e-4
   for key in ['scene_yaw','scene_pitch']:assert abs(captures['snap']['view_state'][key]/15-round(captures['snap']['view_state'][key]/15))<1e-6
   for name in ['uv-orbit','uv-pan']:
    c=captures[name];assert error(rows(c['before_view_state']),rows(c['view_state']))<1e-6
   assert captures['uv-orbit']['view_state']['uv_center']==captures['uv-orbit']['before_view_state']['uv_center'];assert captures['uv-pan']['view_state']['uv_center']!=captures['uv-pan']['before_view_state']['uv_center']
   assert captures['focus-cancel']['view_state']['trackball']is False and captures['focus-cancel']['view_state']['active_gesture']=='Idle'
   c=captures['next-turntable'];assert abs(c['view_state']['scene_yaw']-(c['before_view_state']['scene_yaw']-30*.35))<1e-4;assert abs(c['view_state']['scene_pitch']-max(-90,min(90,c['before_view_state']['scene_pitch']+20*.35)))<1e-4
   assert captures['released-idle']['frame']-captures['released-idle']['before_frame']>=12
   assert captures['released-idle']['view_state']['scene_view_matrix']==captures['released-idle']['before_view_state']['scene_view_matrix'], 'Camera continued moving after release'
   assert captures['persist-enabled']['view_state']['trackball']is True
  else:assert captures['disabled']['view_state']['trackball']is False
  for c in captures.values():
   ui=c['trackball_control'];assert ui['visible'] and ui['enabled'] and ui['same_toolbar'] and ui['checked']==c['view_state']['trackball']
   assert ui['rect'][0]>=ui['symmetry_rect'][0]+ui['symmetry_rect'][2] and abs(ui['rect'][1]-ui['symmetry_rect'][1])<max(ui['rect'][3],ui['symmetry_rect'][3]), 'Trackball is not beside Symmetry'
   assert c['view_state']['device_pixel_ratio']==1, 'This runtime family explicitly witnesses DPR1'
  for name in (['initial']if reload else ['trackball','turntable','persist-enabled']):
   pick=captures[name]['trackball_pick'];assert math.isfinite(pick['max_error']) and pick['max_error']<=2e-4,(name,pick)
   assert max(abs(a-b)for a,b in zip(pick['actual_uv'],pick['expected_uv']))==pick['max_error']
   pa,pb,pc=pick['projected_vertices'];edges=[sum((x-y)**2 for x,y in zip(u,v))for u,v in [(pa,pb),(pa,pc),(pb,pc)]];area2=abs((pb[0]-pa[0])*(pc[1]-pa[1])-(pb[1]-pa[1])*(pc[0]-pa[0]))
   assert area2>=16 and math.sqrt(min(edges))>=2 and area2/max(edges)>=.1
   assert abs(area2-pick['projected_area2'])<1e-9 and abs(math.sqrt(min(edges))-pick['minimum_edge'])<1e-9
   assert 0<=pick['geometric_rank']<pick['geometric_candidates']
   assert max(abs(sum(uv[k]for uv in pick['original_uv'])/3-pick['expected_uv'][k])for k in range(2))<1e-12
  assert captures['disabled']['trackball_control']['pointer_clicks']==(1 if reload else 2)
  assert captures[list(captures)[-1]]['trackball_control']['pointer_clicks']==(1 if reload else 5)
  preference_file=preferences/'BlenderTextureStudio/Preferences.conf';settings=configparser.ConfigParser();settings.read(preference_file)
  assert settings.getboolean('Viewport','trackball') is (not reload), 'Actual private preference file differs from clicked state'
  record['preference_file_sha256']=sha(preference_file)
  guard();no_other_host();record.update(complete=True,proof_sha256=sha(run/'navigation.json'),private_display=json.loads((run/'private-display.json').read_text()),stages=list(captures),document_and_history_unchanged=True);save()
 guard();result.update(complete=True,actual_exit=0,source_and_binary_unchanged=True);save()
 (out/'verified.json').write_text(json.dumps(result,indent=2)+'\n')
except BaseException as error:
 if active is not None:
  if active.poll() is None:
   os.killpg(active.pid,signal.SIGTERM)
   try:active.wait(timeout=5)
   except subprocess.TimeoutExpired:os.killpg(active.pid,signal.SIGKILL);active.wait(timeout=10)
  result['runs'][-1]['actual_exit']=active.returncode
 result.update(actual_exit=1,error=repr(error));save();raise
print(json.dumps(result,indent=2))
