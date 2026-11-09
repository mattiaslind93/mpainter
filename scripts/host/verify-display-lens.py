# SPDX-License-Identifier: GPL-3.0-or-later
"""Native GPU lens distortion and inverse picking; no changes to authored 2D maps."""
from parity_test_environment import private_display
import argparse,json,os,re,subprocess,math
from pathlib import Path
from PIL import Image,ImageChops,ImageStat
p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);a=p.parse_args();a.output=a.output.resolve();a.output.mkdir(parents=True,exist_ok=True);root=Path(__file__).resolve().parents[2]
env=dict(os.environ,LANG='sv_SE.UTF-8',LC_ALL='sv_SE.UTF-8',DISPLAY=private_display(),XDG_RUNTIME_DIR='/run/user/1000',XDG_CONFIG_HOME=str(a.output/'config'),BTS_DISPLAY_POST_DEMO='1',BTS_DISPLAY_LENS_DEMO='1',BTS_SHADER_DEMO='1',BTS_SKEW_DEMO='1')
cmd=['bash',str(root/'scripts/host/run-host.sh'),'--workspace','--gui-fix-demo',str(a.output),'--gui-fix-mesh',str(root/'native/tests/fixtures/single_tile.obj'),'--gui-fix-resolution','128','--library-dir',str(a.output/'shelf'),'--recovery-dir',str(a.output/'recovery')]
with (a.output/'run.log').open('w') as log:
 proc=subprocess.Popen(cmd,env=env,stdout=log,stderr=subprocess.STDOUT)
 try:code=proc.wait(timeout=360)
 except subprocess.TimeoutExpired:proc.terminate();proc.wait(timeout=15);raise
assert code==0,(code,(a.output/'run.log').read_text()[-3500:])
d=json.loads((a.output/'display-lens.json').read_text());assert d['pid']==proc.pid;c={x['stage']:x for x in d['captures']};baseline=c['baseline']
for name,s in c.items():
 assert s['panel_visible'] and s['decimal']=='.' and not s['shader_failures'],name
 assert (name=='painted' or s['document']==baseline['document']) and s['source_obj_hash']==baseline['source_obj_hash'],name
 if name not in ['opened','restored','paint-ready','painted','undo-paint']:
  for field in ['scene_center','scene_yaw','scene_pitch','scene_distance','scene_projection','uv_center','uv_vertical_extent']:assert s['view'][field]==baseline['view'][field],(name,field)
 assert len(s.get('lens_probe_uv',[]))==2,(name,s.get('lens_probe_screen'))
 assert max(abs(x-y) for x,y in zip(s['lens_probe_uv'],baseline['lens_probe_uv']))<1e-5,(name,s['lens_probe_uv'],baseline['lens_probe_uv'])
 assert [math.floor(x*128) for x in s['lens_probe_uv']]==[math.floor(x*128) for x in baseline['lens_probe_uv']],name
 assert not s['lens_corner_hit'],name
def settings(stage):return c[stage]['environment']['post_effects']
assert settings('disabled-retained')['lens_roundness']>.29 and settings('disabled-retained')['lens_smoothness']==2
assert settings('undo-off')['lens'] and not settings('redo-off')['lens']
assert c['saved']['environment']==c['opened']['environment']
assert not settings('restored')['enabled'] and not settings('restored')['lens']
assert math.dist(c['barrel']['lens_probe_screen'],baseline['lens_probe_screen'])>1
images={name:Image.open(a.output/(name+'-viewport.png')).convert('RGB') for name in c}
def crop(stage,view):
 pane=next(p for p in baseline['view']['panes'] if p['view']==view);x,y,w,h=pane['rect'];return images[stage].crop((x+12,y+42,x+w-12,y+h-24))
def diff(first,second,view='3D'):return sum(ImageStat.Stat(ImageChops.difference(crop(first,view),crop(second,view))).mean)/(3*255)
for name in c:
 if name=='painted':continue
 assert diff('baseline',name,'2D')<.0005,(name,diff('baseline',name,'2D'))
for name in ['neutral','disabled-retained','redo-off']:assert diff('baseline',name)<.0015,(name,diff('baseline',name))
for name in ['barrel','pincushion','wide','round','smooth']:assert diff('baseline',name)>.001,(name,diff('baseline',name))
assert diff('pincushion','wide')>.001
assert diff('round','smooth')>.0001
assert c['painted']['hash']!=c['paint-ready']['hash'] and c['undo-paint']['hash']==c['paint-ready']['hash']
assert c['painted']['undo_count']==c['paint-ready']['undo_count']+1
log=(a.output/'run.log').read_text();assert f'BTS_SHUTDOWN clean pid={proc.pid}' in log;assert not re.search(r'BTS_HOST_FAILED|BTS_DRAW_FAILED|Shader compilation failed',log,re.I)
result=dict(pid=proc.pid,locale='sv_SE.UTF-8',gpu_lens=True,barrel_difference=diff('baseline','barrel'),pincushion_difference=diff('baseline','pincushion'),fov_difference=diff('pincushion','wide'),roundness_difference=diff('baseline','round'),feather_difference=diff('round','smooth'),uv_difference=0,inverse_picking_same_texel=True,real_stroke_one_undo=True,undo_redo=True,packed_open=True,document_mesh_uv_camera_preserved=True)
(a.output/'verified.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
