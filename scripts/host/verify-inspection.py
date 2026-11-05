"""Native C/B/M and both viewport menus: unlit data, shared selection, frozen document/cameras."""
from parity_test_environment import private_display
import argparse,json,os,re,subprocess
from pathlib import Path
from PIL import Image,ImageChops,ImageStat
p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);a=p.parse_args();a.output=a.output.resolve();a.output.mkdir(parents=True,exist_ok=True);root=Path(__file__).resolve().parents[2]
env=dict(os.environ,LANG='sv_SE.UTF-8',LC_ALL='sv_SE.UTF-8',DISPLAY=private_display(),XDG_RUNTIME_DIR='/run/user/1000',XDG_CONFIG_HOME=str(a.output/'config'),BTS_INSPECTION_DEMO='1',BTS_SKEW_DEMO='1')
cmd=['bash',str(root/'scripts/host/run-host.sh'),'--workspace','--gui-fix-demo',str(a.output),'--gui-fix-mesh',str(root/'native/tests/fixtures/single_tile.obj'),'--gui-fix-resolution','512','--library-dir',str(a.output/'shelf'),'--recovery-dir',str(a.output/'recovery')]
with (a.output/'run.log').open('w') as log:
 proc=subprocess.Popen(cmd,env=env,stdout=log,stderr=subprocess.STDOUT)
 try:code=proc.wait(timeout=300)
 except subprocess.TimeoutExpired:proc.terminate();proc.wait(timeout=15);raise
assert code==0,(code,(a.output/'run.log').read_text()[-3000:])
d=json.loads((a.output/'inspection.json').read_text());assert d['pid']==proc.pid;c={s['stage']:s for s in d['captures']};initial=c['material']
expected={'material':(-1,-1,'Material'),'color':(0,-1,'Base Color'),'metallic':(1,-1,'Metallic'),'color-reverse':(0,-1,'Base Color'),'color-light0':(0,-1,'Base Color'),'color-light90':(0,-1,'Base Color'),'id':(2,2,'ID'),'ao':(2,3,'Ambient occlusion'),'id-menu-uv':(2,2,'ID'),'normal-menu-3d':(4,-1,'Bump'),'roughness-menu-uv':(2,-1,'Roughness'),'material-restored':(-1,-1,'Material'),'material-light0':(-1,-1,'Material'),'material-light90':(-1,-1,'Material'),'ao-menu-3d':(2,3,'Ambient occlusion'),'id-reverse':(2,2,'ID'),'material-menu-uv':(-1,-1,'Material')}
for name,(channel,role,label) in expected.items():
 s=c[name];mode=s['inspection'];assert (mode['channel'],mode['map'],mode['label'])==(channel,role,label),(name,mode)
 assert s['document']==initial['document'],name
 assert s['selected_set']==initial['selected_set'] and s['layer_context']==initial['layer_context'],name
 assert s['source_obj_hash']==initial['source_obj_hash'],name
 assert s['decimal']=='.' and not s['shader_failures']
 for field in ['scene_center','scene_yaw','scene_pitch','scene_distance','scene_projection','scene_vertical_extent','uv_center','uv_vertical_extent','selected_material_session_id']:
  assert s['view'][field]==initial['view'][field],(name,field)
 expected_panes=initial['view']['panes'];assert s['view']['panes']==expected_panes,name
menus=d['menus'];assert [m['pane'] for m in menus]==['2D','3D','2D','3D','2D']
for m,checked in zip(menus,['inspectMeshMap3','inspectMeshMap2','inspectChannel4','inspectMaterial','inspectMeshMap2']):
 assert [x['text'] for x in m['entries'] if x['section']]==['Lighting','Single channel','Mesh maps']
 assert [x['name'] for x in m['entries'] if x['checked']]==[checked],m
 assert all(x['name']!='inspectChannel3' and not (x['name'].startswith('inspectChannel') and x['text']=='Normal') for x in m['entries'])
images={name:Image.open(a.output/(name+'-viewport.png')).convert('RGB') for name in c}
def diff(first,second,view):
 pane=next(x for x in initial['view']['panes'] if x['view']==view);x,y,w,h=pane['rect'];box=(x+12,y+42,x+w-12,y+h-24)
 return sum(ImageStat.Stat(ImageChops.difference(images[first].crop(box),images[second].crop(box))).mean)/(3*255)
color={view:diff('color-light0','color-light90',view) for view in ['3D','2D']}
assert all(v<.0005 for v in color.values()),color
lit=diff('material-light0','material-light90','3D');assert lit>.001,lit
assert diff('material-light0','material-light90','2D')<.0005
assert diff('id','ao','3D')>.001 and diff('id','ao','2D')>.001
for image in images.values():assert max(ImageStat.Stat(image).stddev)>8
log=(a.output/'run.log').read_text();assert f'BTS_SHUTDOWN clean pid={proc.pid}' in log;assert not re.search(r'BTS_HOST_FAILED|BTS_DRAW_FAILED|Shader compilation failed',log,re.I)
result=dict(pid=proc.pid,locale='sv_SE.UTF-8',channel_cycle_reverse=True,map_cycle_reverse=True,material_restore=True,both_viewport_menus=True,grouped_menu_correct_active_check=True,document_pixels_mesh_uv_selection_camera_preserved=True,solo_light_rotation_difference=color,material_light_rotation_difference=lit,id_ao_images_differ=True)
(a.output/'verified.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
