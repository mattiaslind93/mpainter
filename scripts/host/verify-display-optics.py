"""Native EEVEE depth-driven DOF and GPU glare, retained settings, no authored-data edits."""
from parity_test_environment import private_display
import argparse,json,os,re,subprocess
from pathlib import Path
from PIL import Image,ImageChops,ImageStat
p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);a=p.parse_args();a.output=a.output.resolve();a.output.mkdir(parents=True,exist_ok=True);root=Path(__file__).resolve().parents[2]
env=dict(os.environ,LANG='sv_SE.UTF-8',LC_ALL='sv_SE.UTF-8',DISPLAY=private_display(),XDG_RUNTIME_DIR='/run/user/1000',XDG_CONFIG_HOME=str(a.output/'config'),BTS_DISPLAY_POST_DEMO='1',BTS_DISPLAY_OPTICS_DEMO='1',BTS_SHADER_DEMO='1',BTS_SKEW_DEMO='1')
cmd=['bash',str(root/'scripts/host/run-host.sh'),'--workspace','--gui-fix-demo',str(a.output),'--gui-fix-mesh',str(root/'fixtures/single_tile.obj'),'--gui-fix-resolution','128','--library-dir',str(a.output/'shelf'),'--recovery-dir',str(a.output/'recovery')]
with (a.output/'run.log').open('w') as log:
 proc=subprocess.Popen(cmd,env=env,stdout=log,stderr=subprocess.STDOUT)
 try:code=proc.wait(timeout=360)
 except subprocess.TimeoutExpired:proc.terminate();proc.wait(timeout=15);raise
assert code==0,(code,(a.output/'run.log').read_text()[-3500:])
d=json.loads((a.output/'display-optics.json').read_text());assert d['pid']==proc.pid;c={x['stage']:x for x in d['captures']};baseline=c['baseline']
for name,s in c.items():
 assert s['panel_visible'] and s['decimal']=='.' and not s['shader_failures'],name
 assert s['document']==baseline['document'] and s['source_obj_hash']==baseline['source_obj_hash'],name
 if name not in ['opened','restored']:
  for field in ['scene_center','scene_yaw','scene_pitch','scene_distance','scene_projection','uv_center','uv_vertical_extent']:assert s['view'][field]==baseline['view'][field],(name,field)
def settings(stage):return c[stage]['environment']['post_effects']
for name in ['near-focus','defocused','combined','saved','opened']:assert c[name]['environment']['post_depth_enabled'],name
for name in ['aperture-zero','dof-off-retained','zero-again','restored']:assert c[name]['environment']['post_depth_enabled']==baseline['environment']['post_depth_enabled'],name
assert settings('dof-off-retained')['aperture']==4 and settings('dof-off-retained')['focus_distance']==100
assert settings('glare-zero')['glare'] and settings('glare-zero')['glare_luminance']==0
assert settings('undo-glare')['glare_luminance']==1 and settings('redo-zero')['glare_luminance']==0
assert settings('fog')['glare_shape']==1 and settings('ghosts')['glare_shape']==2
assert c['saved']['environment']==c['opened']['environment']
assert not settings('restored')['enabled'] and not settings('restored')['dof'] and not settings('restored')['glare']
images={name:Image.open(a.output/(name+'-viewport.png')).convert('RGB') for name in c}
def crop(stage,view):
 pane=next(p for p in baseline['view']['panes'] if p['view']==view);x,y,w,h=pane['rect'];return images[stage].crop((x+12,y+42,x+w-12,y+h-24))
def diff(first,second,view='3D'):return sum(ImageStat.Stat(ImageChops.difference(crop(first,view),crop(second,view))).mean)/(3*255)
for name in c:assert diff('baseline',name,'2D')<.0005,(name,diff('baseline',name,'2D'))
for name in ['aperture-zero','dof-off-retained','zero-again','glare-zero','redo-zero']:assert diff('baseline',name)<.0015,(name,diff('baseline',name))
focus=diff('near-focus','defocused');assert focus>.001,focus
strong=diff('baseline','streaks');weak=diff('baseline','weak-streaks');assert strong>weak+.001 and weak>.0005,(strong,weak)
remap=diff('streaks','spectral');assert remap>.0005,remap
for name in ['fog','ghosts']:assert diff('baseline',name)>.001,(name,diff('baseline',name))
log=(a.output/'run.log').read_text();assert f'BTS_SHUTDOWN clean pid={proc.pid}' in log;assert not re.search(r'BTS_HOST_FAILED|BTS_DRAW_FAILED|Shader compilation failed',log,re.I)
result=dict(pid=proc.pid,locale='sv_SE.UTF-8',gpu_defocus=True,depth_pass_on_demand_and_retained_original=True,focus_change_difference=focus,glare_streaks=strong,weaker_streaks=weak,spectral_remap_difference=remap,native_fog_and_ghosts=True,zero_values_bypass=True,uv_difference=0,undo_redo=True,packed_open=True,document_mesh_uv_camera_preserved=True)
(a.output/'verified.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
