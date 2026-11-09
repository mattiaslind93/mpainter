# SPDX-License-Identifier: GPL-3.0-or-later
"""Real Qt shader identity/parameters/history, ordinary Blender nodes and packed roundtrip."""
from parity_test_environment import private_display
import argparse,json,os,re,subprocess
from pathlib import Path
from PIL import Image,ImageStat,ImageChops
p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);p.add_argument('--check-only',action='store_true');a=p.parse_args();a.output=a.output.resolve();a.output.mkdir(parents=True,exist_ok=True);root=Path(__file__).resolve().parents[2]
# Two material slots on the existing sphere/companion fixture, without editing artist assets.
obj=(root/'native/tests/fixtures/single_tile.obj').read_text();obj=obj.replace('mtllib single_tile.mtl','mtllib shader-sets.mtl');index=0
lines=[]
for line in obj.splitlines():
 if line.startswith('usemtl '):line='usemtl '+['SphereSurface','CubeSurface'][index];index+=1
 lines.append(line)
mesh=a.output/'shader-sets.obj';mesh.write_text('\n'.join(lines)+'\n');mtl=(root/'native/tests/fixtures/single_tile.mtl').read_text();(a.output/'shader-sets.mtl').write_text(mtl.replace('FixtureSurface','SphereSurface')+'\n'+mtl.replace('FixtureSurface','CubeSurface'))
env=dict(os.environ,LANG='sv_SE.UTF-8',LC_ALL='sv_SE.UTF-8',DISPLAY=private_display(),XDG_RUNTIME_DIR='/run/user/1000',XDG_CONFIG_HOME=str(a.output/'config'),BTS_SHADER_DEMO='1',BTS_SKEW_DEMO='1')
cmd=['bash',str(root/'scripts/host/run-host.sh'),'--workspace','--gui-fix-demo',str(a.output),'--gui-fix-mesh',str(mesh),'--gui-fix-resolution','128','--library-dir',str(a.output/'shelf'),'--recovery-dir',str(a.output/'recovery')]
if not a.check_only:
 with (a.output/'run.log').open('w') as log:
  proc=subprocess.Popen(cmd,env=env,stdout=log,stderr=subprocess.STDOUT)
  try:code=proc.wait(timeout=300)
  except subprocess.TimeoutExpired:proc.terminate();proc.wait(timeout=15);raise
 assert code==0,(code,(a.output/'run.log').read_text()[-3000:])
d=json.loads((a.output/'shaders.json').read_text());assert a.check_only or d['pid']==proc.pid;c={x['stage']:x for x in d['captures']}
def library(stage):return c[stage]['document']['shader_instances']
def instances(stage):return {v['id']:v for v in library(stage)['instances']}
def names(stage):return [x['shader'] for x in c[stage]['rows']]
for stage in ['initial','renamed','rename-undo','rename-redo','shared-parameters','inherited','independent','docked','shader-undo','shader-redo']:
 assert c[stage]['dialog_visible'] and c[stage]['displacement_visible'],(stage,c[stage]['dialog_visible'],c[stage]['displacement_visible'])
 assert c[stage]['displacement_parent']=='shaderSettingsContent',stage
assert names('initial')==['Main shader']*2
assert names('renamed')==['BTS Shared Reference']*2
assert names('rename-undo')==['Main shader']*2
assert names('rename-redo')==['BTS Shared Reference']*2
assert not c['rename-undo']['shader_undo'] and c['rename-undo']['shader_redo']
assert len(instances('inherited'))==2
def parameters(normal_intensity,ao_intensity):
 value=next(iter(instances('initial').values()))['parameters'].copy();value.update(normal_intensity=normal_intensity,ao_intensity=ao_intensity);return value
assert all(x['parameters']==parameters(2,.75) for x in instances('inherited').values())
assert sorted(names('independent'))==['BTS Shared Reference','Independent shader']
ind=instances('independent');independent=next(x for x in ind.values() if x['name']=='Independent shader');shared=next(x for x in ind.values() if x['name']=='BTS Shared Reference')
assert independent['parameters']==parameters(0,.25)
assert shared['parameters']==parameters(2,.75)
assert library('paint-history-add')==library('paint-history-undo')==library('independent')
assert c['paint-history-add']['undo_count']==c['paint-history-undo']['undo_count']+1
assert library('shader-undo')==library('inherited') and library('shader-redo')==library('independent')
assert c['saved']['document']==c['opened']['document']
assert not c['opened']['shader_undo'] and not c['opened']['shader_redo']
for name,s in c.items():
 assert s['decimal']=='.' and not s['shader_failures'],name
 if name not in ['paint-history-add']:
  assert s['document']['texture_sets']==c['initial']['document']['texture_sets'],name
 for field in ['scene_center','scene_yaw','scene_pitch','scene_distance','scene_projection','uv_center','uv_vertical_extent']:
  if name!='opened':assert s['view'][field]==c['initial']['view'][field],(name,field)
for stage in ['shared-parameters','independent','opened']:
 lib=library(stage);sets=c[stage]['document']['texture_sets'];values=instances(stage)
 for s in sets:
  expected=values[lib['texture_sets'][s['id']]]['parameters'];node=c[stage]['shader_nodes'][s['name']]
  # Paint target initialization provides native normal nodes; AO is checked if allocated.
  assert node['normal_intensity']==expected['normal_intensity'],(stage,s['name'],node,expected)
  assert node['ao_intensity']==expected['ao_intensity'],(stage,s['name'],node,expected)
image=Image.open(a.output/'independent-viewport.png').convert('RGB');assert max(ImageStat.Stat(image).stddev)>8
baseline=Image.open(a.output/'shared-parameters-viewport.png').convert('RGB')
differences={}
for pane in c['independent']['view']['panes']:
 x,y,w,h=pane['rect'];box=(x+12,y+42,x+w-12,y+h-24)
 differences[pane['view']]=sum(ImageStat.Stat(ImageChops.difference(image.crop(box),baseline.crop(box))).mean)/(3*255)
assert differences['3D']>.0001,differences
assert differences['2D']<.0005,differences
log=(a.output/'run.log').read_text();assert f'BTS_SHUTDOWN clean pid={d["pid"]}' in log;assert not re.search(r'BTS_HOST_FAILED|BTS_DRAW_FAILED|Shader compilation failed',log,re.I)
result=dict(appearance_difference=differences,pid=d['pid'],locale='sv_SE.UTF-8',shared_identity_rename=True,inherited_independent_parameters=True,separate_shader_and_paint_history=True,stock_blender_node_parameters=True,dock_undock=True,packed_roundtrip=True,authored_pixels_geometry_uv_camera_preserved=True)
(a.output/'verified.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
