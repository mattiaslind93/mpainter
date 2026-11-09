# SPDX-License-Identifier: GPL-3.0-or-later
"""Native EEVEE/Cycles subsurface, independent controls, unlit UV and packed project state."""
from parity_test_environment import private_display
import argparse,json,os,re,subprocess
from pathlib import Path
from PIL import Image,ImageChops,ImageStat
p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);p.add_argument('--check-only',action='store_true');p.add_argument('--cache',type=Path);a=p.parse_args();a.output=a.output.resolve();a.output.mkdir(parents=True,exist_ok=True);root=Path(__file__).resolve().parents[2]
obj=(root/'native/tests/fixtures/single_tile.obj').read_text().replace('mtllib single_tile.mtl','mtllib subsurface.mtl');lines=[];slot=0
for line in obj.splitlines():
 if line.startswith('usemtl '):line='usemtl '+['SphereSurface','CubeSurface'][slot];slot+=1
 lines.append(line)
mesh=a.output/'subsurface.obj';mesh.write_text('\n'.join(lines)+'\n');mtl=(root/'native/tests/fixtures/single_tile.mtl').read_text();(a.output/'subsurface.mtl').write_text(mtl.replace('FixtureSurface','SphereSurface')+'\n'+mtl.replace('FixtureSurface','CubeSurface'))
env=dict(os.environ,LANG='sv_SE.UTF-8',LC_ALL='sv_SE.UTF-8',DISPLAY=private_display(),XDG_RUNTIME_DIR='/run/user/1000',XDG_CONFIG_HOME=str(a.output/'config'),XDG_CACHE_HOME=str((a.cache or a.output/'cache').resolve()),BTS_SUBSURFACE_DEMO='1',BTS_SHADER_DEMO='1',BTS_SKEW_DEMO='1')
cmd=['bash',str(root/'scripts/host/run-host.sh'),'--workspace','--gui-fix-demo',str(a.output),'--gui-fix-mesh',str(mesh),'--gui-fix-resolution','128','--library-dir',str(a.output/'shelf'),'--recovery-dir',str(a.output/'recovery')]
if not a.check_only:
 with (a.output/'run.log').open('w') as log:
  proc=subprocess.Popen(cmd,env=env,stdout=log,stderr=subprocess.STDOUT)
  try:code=proc.wait(timeout=400)
  except subprocess.TimeoutExpired:proc.terminate();proc.wait(timeout=20);raise
 assert code==0,(code,(a.output/'run.log').read_text()[-4000:])
d=json.loads((a.output/'subsurface.json').read_text());c={s['stage']:s for s in d['captures']};base=c['initial']
if not a.check_only:assert d['pid']==proc.pid
for name,s in c.items():
 assert s['decimal']=='.' and not s['shader_failures'] and s['source_obj_hash']==base['source_obj_hash'],name
 cube=next(x for x in s['document']['texture_sets'] if x['name']=='CubeSurface');original=next(x for x in base['document']['texture_sets'] if x['name']=='CubeSurface');assert cube==original,name
 assert 'subsurface' not in s['shader_nodes']['CubeSurface'],name
 camera_reference=c['opened'] if name in ['opened','cycles','cycles-off','painting-after-render'] else base
 for field in ['scene_center','scene_distance','scene_yaw','scene_pitch','scene_projection','uv_center','uv_vertical_extent']:assert s['view'][field]==camera_reference['view'][field],(name,field)
 assert s['shader_nodes']['SphereSurface']['subsurface']['gate']==(0 if name in ['initial','global-off','global-redo','shader-off','cycles-off','painting-after-render'] else 1),name
 assert s['environment']['subsurface']['enabled']==(name not in ['initial','global-off','global-redo','cycles-off','painting-after-render']),name
 assert s['samples_editable']==(name not in ['initial','global-off','global-redo','cycles','cycles-off','painting-after-render']),name
assert not c['shader-off']['shader_fields_editable']
assert base['environment']['subsurface']['samples']==16 and c['samples32']['environment']['subsurface']['samples']==32
assert c['enabled-zero']['document']==base['document']
assert c['global-off']['document']==c['scattering-one']['document']==c['global-undo']['document']==c['global-redo']['document']
assert c['colored']['document']==c['shader-redo']['document']==c['samples32']['document']==c['saved']['document']==c['opened']['document']==c['cycles']['document']
assert c['shader-undo']['document']!=c['colored']['document']
assert c['scale-zero']['shader_nodes']['SphereSurface']['subsurface']['scale']==0
assert c['translucent']['shader_nodes']['SphereSurface']['subsurface']['method']!=c['scattering-one']['shader_nodes']['SphereSurface']['subsurface']['method']
assert c['cycles']['render']['active'] and c['cycles']['render']['device']=='CUDA' and 'Rendering Done' in c['cycles']['render']['status']
assert not c['painting-after-render']['render']['active']
def diff(first,second,kind=None):
 x=Image.open(a.output/(first+'-viewport.png')).convert('RGB');y=Image.open(a.output/(second+'-viewport.png')).convert('RGB')
 if kind:
  pane=next(x for x in c[first]['view']['panes'] if x['view']==kind);left,top,width,height=pane['rect'];box=(left+12,top+35,left+width-12,top+height-35);x=x.crop(box);y=y.crop(box)
 return sum(ImageStat.Stat(ImageChops.difference(x,y)).mean)/765
visible=diff('scattering-one','global-off','3D');assert visible>.0003,visible
for name in ['initial','enabled-zero','scattering-one','global-off','global-undo','global-redo','shader-off','scale-zero','translucent','colored','shader-undo','shader-redo','samples32','saved']:assert diff('initial',name,'2D')==0,name
for name in ['initial','enabled-zero','shader-off','scale-zero']:assert diff('global-off',name,'3D')<.003,name
assert diff('colored','shader-redo','3D')<.003
assert c['saved']['shader_nodes']==c['opened']['shader_nodes']
assert diff('opened','painting-after-render','2D')==0
on=Image.open(a.output/'scattering-one-viewport.png').convert('RGB');off=Image.open(a.output/'global-off-viewport.png').convert('RGB');delta=ImageChops.difference(on,off);pane=next(x for x in base['view']['panes'] if x['view']=='3D');x,y,w,h=pane['rect'];cube_box=(int(x+w*.65),int(y+h*.5),int(x+w*.93),int(y+h*.9));cube_max=max(high for low,high in delta.crop(cube_box).getextrema());assert cube_max<=1,cube_max
cycles_diff=diff('cycles','cycles-off');assert cycles_diff>.001,cycles_diff
log=(a.output/'run.log').read_text();assert f'BTS_SHUTDOWN clean pid={d["pid"]}' in log
assert not re.search(r'BTS_DRAW_FAILED|Shader compilation failed|BTS_SHARED_CONTEXT_FAILED',log,re.I)
result=dict(pid=d['pid'],locale='sv_SE.UTF-8',eevee_scattering_difference=visible,cycles_scattering_difference=cycles_diff,global_shader_channel_independent=True,real_eevee_kernel_samples_16_32=True,shader_method_scale_color_and_undo=True,packed_roundtrip=True,source_mesh_unchanged=True,camera_and_uv_unchanged_by_controls=True,existing_open_framing_retained=True,cube_material_unaffected=True,cube_max_rgb_difference=cube_max,clean_shutdown=True)
(a.output/'verified.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
