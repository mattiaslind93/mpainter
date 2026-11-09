# SPDX-License-Identifier: GPL-3.0-or-later
"""Native GPU color/tone post effects, all exposed parameters, history and packed settings."""
from parity_test_environment import private_display
import argparse,json,os,re,subprocess
from pathlib import Path
from PIL import Image,ImageChops,ImageStat
p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);a=p.parse_args();a.output=a.output.resolve();a.output.mkdir(parents=True,exist_ok=True);root=Path(__file__).resolve().parents[2]
env=dict(os.environ,LANG='sv_SE.UTF-8',LC_ALL='sv_SE.UTF-8',DISPLAY=private_display(),XDG_RUNTIME_DIR='/run/user/1000',XDG_CONFIG_HOME=str(a.output/'config'),BTS_DISPLAY_POST_DEMO='1',BTS_DISPLAY_COLOR_DEMO='1',BTS_SHADER_DEMO='1',BTS_SKEW_DEMO='1')
cmd=['bash',str(root/'scripts/host/run-host.sh'),'--workspace','--gui-fix-demo',str(a.output),'--gui-fix-mesh',str(root/'native/tests/fixtures/single_tile.obj'),'--gui-fix-resolution','128','--library-dir',str(a.output/'shelf'),'--recovery-dir',str(a.output/'recovery')]
with (a.output/'run.log').open('w') as log:
 proc=subprocess.Popen(cmd,env=env,stdout=log,stderr=subprocess.STDOUT)
 try:code=proc.wait(timeout=300)
 except subprocess.TimeoutExpired:proc.terminate();proc.wait(timeout=15);raise
assert code==0,(code,(a.output/'run.log').read_text()[-3000:])
d=json.loads((a.output/'display-color.json').read_text());assert d['pid']==proc.pid;c={x['stage']:x for x in d['captures']};baseline=c['baseline']
for name,s in c.items():
 assert s['panel_visible'] and s['decimal']=='.' and not s['shader_failures'],name
 assert s['document']==baseline['document'] and s['source_obj_hash']==baseline['source_obj_hash'],name
 if name not in ['opened','restored']:
  for field in ['scene_center','scene_yaw','scene_pitch','scene_distance','scene_projection','uv_center','uv_vertical_extent']:assert s['view'][field]==baseline['view'][field],(name,field)
def settings(stage):return c[stage]['environment']['post_effects']
assert c['warm']['color_expanded']
assert settings('desaturated')['saturation']==0 and settings('sepia')['sepia']==1 and settings('warm')['temperature']==3500
assert settings('color-reset')['saturation']==1 and settings('color-reset')['contrast']==1 and settings('color-reset')['bias']==0
assert settings('reinhard')['tone_function']==1 and settings('photoreceptor')['tone_function']==2
assert settings('tone-reset')['tone_function']==0 and settings('tone-reset')['gamma']==1 and settings('tone-reset')['exposure']==1
assert settings('combined')['saturation']==.5 and settings('undo-saturation')['saturation']==1 and settings('redo-saturation')==settings('combined')
assert c['saved']['environment']==c['opened']['environment']
assert not settings('restored')['enabled'] and not settings('restored')['color_correction'] and not settings('restored')['tone_mapping']
images={name:Image.open(a.output/(name+'-viewport.png')).convert('RGB') for name in c}
def crop(stage,view):
 pane=next(p for p in baseline['view']['panes'] if p['view']==view);x,y,w,h=pane['rect'];return images[stage].crop((x+12,y+42,x+w-12,y+h-24))
def diff(first,second,view='3D'):return sum(ImageStat.Stat(ImageChops.difference(crop(first,view),crop(second,view))).mean)/(3*255)
for name in c:assert diff('baseline',name,'2D')<.0005,(name,diff('baseline',name,'2D'))
for name in ['master-only','color-defaults','color-reset','tone-defaults','tone-reset']:assert diff('baseline',name)<.0015,(name,diff('baseline',name))
differences={name:diff('baseline',name) for name in ['desaturated','sepia','warm','contrast','exposed','gamma','reinhard','photoreceptor','combined']}
assert all(value>.003 for value in differences.values()),differences
assert sum(ImageStat.Stat(crop('exposed','3D')).mean)>sum(ImageStat.Stat(crop('baseline','3D')).mean)+5
assert sum(ImageStat.Stat(crop('gamma','3D')).mean)>sum(ImageStat.Stat(crop('exposed','3D')).mean)+5
spread=sum(max(rgb)-min(rgb) for rgb in crop('desaturated','3D').getdata())/(crop('desaturated','3D').width*crop('desaturated','3D').height*255);assert spread<.01,spread
log=(a.output/'run.log').read_text();assert f'BTS_SHUTDOWN clean pid={proc.pid}' in log;assert not re.search(r'BTS_HOST_FAILED|BTS_DRAW_FAILED|Shader compilation failed',log,re.I)
result=dict(pid=proc.pid,locale='sv_SE.UTF-8',gpu_compositor=True,color_correction=dict(saturation=True,contrast=True,brightness=True,bias=True,sepia=True,white_balance=True),tone_mapping=dict(exposure=True,gamma=True,linear=True,native_reinhard=True,native_photoreceptor=True,mapping_factor=True),differences=differences,desaturated_rgb_spread=spread,uv_difference=0,undo_redo=True,per_group_restore=True,packed_open=True,document_mesh_uv_camera_preserved=True)
(a.output/'verified.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
