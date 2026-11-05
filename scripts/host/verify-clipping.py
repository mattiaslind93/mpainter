"""Native clipping reproduction and live app-global Viewport preferences."""
from parity_test_environment import private_display
import argparse,json,os,shutil,subprocess,re
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);p.add_argument('--baseline',action='store_true');p.add_argument('--before',type=Path,help='Instrumented pre-fix native capture directory');a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True);root=Path(__file__).resolve().parents[2]
(a.output/'clip-plane.obj').write_text('mtllib clip-plane.mtl\no ClipPlane\nv -1 -1 0\nv 1 -1 0\nv 1 1 0\nv -1 1 0\nvt 0 0\nvt 1 0\nvt 1 1\nvt 0 1\nvn 0 0 1\nusemtl ClipSurface\nf 1/1/1 2/2/1 3/3/1 4/4/1\n')
(a.output/'clip-plane.mtl').write_text('newmtl ClipSurface\nKd 1 1 1\nPr .4\n')
shutil.copyfile(root/'replica/specs/fixtures/camera_card.dae',a.output/'camera-card.dae')
env=dict(os.environ,LANG='sv_SE.UTF-8',LC_ALL='sv_SE.UTF-8',DISPLAY=private_display(),XDG_RUNTIME_DIR='/run/user/1000',BTS_CLIPPING_DEMO='1',XDG_CONFIG_HOME=str(a.output/'config'))
if a.baseline:env['BTS_CLIPPING_BASELINE']='1'
command=['bash',str(root/'scripts/host/run-host.sh'),'--workspace','--gui-fix-demo',str(a.output),'--gui-fix-mesh',str(root/'native/tests/fixtures/single_tile.obj'),'--gui-fix-resolution','256','--recovery-dir',str(a.output/'recovery')]
with (a.output/'run.log').open('w') as log:
 process=subprocess.Popen(command,env=env,stdout=log,stderr=subprocess.STDOUT)
 try:code=process.wait(timeout=180)
 except subprocess.TimeoutExpired:process.terminate();process.wait(timeout=15);raise
assert code==0,code
proof=json.loads((a.output/'clipping.json').read_text());assert proof['pid']==process.pid
c={v['stage']:v for v in proof['captures']}
assert all(v['decimal']=='.' and not v['shader_failures'] for v in c.values())
assert c['imported-camera']['view']['clip']['scene_camera']
assert abs(c['imported-camera']['view']['clip']['near']-.1)<1e-7
assert c['imported-camera']['view']['clip']['far']==100
assert not c['detached-camera']['view']['clip']['scene_camera']
if not a.baseline:
 def clip(stage):return c[stage]['view']['clip']
 for stage in ['demo-framed','plane-framed','displaced-framed','displaced-close','negative-close','detached-camera','detail-point02']:
  assert clip(stage)['near']==.01 and clip(stage)['far']==1000,stage
 for stage in ['displaced-framed','displaced-close','negative-close','detail-point02','preferences-near-point001']:
  rgb=c[stage]['center_rgb'];assert min(int(rgb[i:i+2],16) for i in [1,3,5])>150,(stage,rgb)
 assert abs(c['detail-point02']['view']['scene_distance']-.27)<1e-4
 assert clip('preferences-near-point1')['near']==.1 and c['preferences-near-point1']['center_rgb']=='#3f3f3f'
 assert clip('preferences-near-point001')['near']==.001
 assert clip('invalid-rejected')['near']==.001 and clip('invalid-rejected')['far']==1000
 for stage in ['preferences-custom','preferences-accepted','cancel-restored','defaults-cancel-restored']:
  assert clip(stage)['near']==.005 and clip(stage)['far']==500,stage
 assert clip('defaults-preview')['near']==.01 and clip('defaults-preview')['far']==1000
 assert clip('cancel-preview')['near']==.1
 assert clip('orthographic')['near']==-500 and clip('orthographic')['far']==500
 for stage in ['detail-point02','preferences-custom','preferences-accepted','cancel-restored','defaults-cancel-restored','orthographic','plane-refocused']:
  assert c[stage]['document']==c['detail-point02']['document'] and c[stage]['undo_count']==c['detail-point02']['undo_count'],stage
  assert max(abs(v-.5) for v in c[stage]['picked_uv'])<1e-4,(stage,c[stage].get('picked_uv'))
 assert any(m['content'] and m['method']==2 and m['gpu_displacement_outputs'] for m in c['displaced-close']['geometry']['materials'])
 restart_env=dict(env,BTS_CLIPPING_RESTART='1')
 with (a.output/'restart.log').open('w') as log:
  restart=subprocess.run(command,env=restart_env,stdout=log,stderr=subprocess.STDOUT,timeout=60)
 assert restart.returncode==0
 restarted=json.loads((a.output/'clipping-restart.json').read_text())['captures'][0]
 assert not restarted['has_document'] and restarted['view']['clip']['near']==.005 and restarted['view']['clip']['far']==500
 for name in ['run.log','restart.log']:
  text=(a.output/name).read_text();assert not re.search(r'Shader compilation failed|error C\d+|BTS_DRAW_FAILED|BTS_HOST_FAILED',text,re.I),text[-3000:]
 if a.before:
  from PIL import Image,ImageChops,ImageStat
  one=Image.open(a.before/'demo-framed.png').convert('RGB');two=Image.open(a.output/'demo-framed.png').convert('RGB');assert one.size==two.size
  normal_delta=sum(ImageStat.Stat(ImageChops.difference(one,two)).mean)/(3*255)
  assert normal_delta<.03,normal_delta
  (a.output/'normal-comparison.json').write_text(json.dumps(dict(mean_absolute_difference=normal_delta,depth_format='32-bit float + stencil',near=.01,far=1000))+'\n')
result=dict(pid=process.pid,locale='sv_SE.UTF-8',baseline=a.baseline,clips={k:v['view']['clip']|dict(distance=v['view']['scene_distance'],center_rgb=v['center_rgb']) for k,v in c.items()})
if not a.baseline:result.update(live_preferences=True,invalid_rejected=True,cancel_restores=True,persistence_after_restart=True,app_global_not_project=True,displacement_visible_at_point02=True,original_surface_picking=True,orthographic_symmetric=True)
(a.output/'measured.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
