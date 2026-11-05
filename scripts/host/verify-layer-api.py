"""PD140: real console edits, viewport buffers, native Undo and packed projects."""
from parity_test_environment import private_display
import argparse,json,os,shutil,subprocess
from pathlib import Path
from PIL import Image
p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);p.add_argument('--cache',type=Path);p.add_argument('--check-only',action='store_true');a=p.parse_args();a.output=a.output.resolve();a.output.mkdir(parents=True,exist_ok=True);root=Path(__file__).resolve().parents[2]
fixture=root/'native/tests/fixtures/two_materials.obj'
for name in ['two_materials.obj','two_materials.mtl']:shutil.copyfile(fixture.parent/name,a.output/name)
env=dict(os.environ,LANG='sv_SE.UTF-8',LC_ALL='sv_SE.UTF-8',DISPLAY=private_display(),XDG_RUNTIME_DIR='/run/user/1000',XDG_CONFIG_HOME=str(a.output/'config'),XDG_DATA_HOME=str(a.output/'data'),XDG_CACHE_HOME=str((a.cache or a.output/'cache').resolve()),BTS_LAYER_API_DEMO=str(a.output),BTS_LAYER_API_MESH=str(a.output/'two_materials.obj'))
if not a.check_only:
 with (a.output/'run.log').open('w')as log:
  process=subprocess.Popen(['bash',str(root/'scripts/host/run-host.sh'),'--workspace','--library-dir',str(a.output/'shelf'),'--recovery-dir',str(a.output/'recovery')],env=env,stdout=log,stderr=subprocess.STDOUT)
  try:code=process.wait(timeout=480)
  except subprocess.TimeoutExpired:process.terminate();process.wait(timeout=20);raise
 assert code==0,(code,(a.output/'run.log').read_text()[-6000:])
d=json.loads((a.output/'layer-api.json').read_text());script=json.loads((a.output/'layer-script.json').read_text());c={s['stage']:s for s in d['captures']}
if not a.check_only:assert d['pid']==script['pid']==process.pid
assert len(c)==13 and len(script['errors'])==5 and script['events']>0
first=c['before']['textures'][0]['id'];other=c['before']['textures'][1]['id']
def native(stage,uid):return next(s for s in c[stage]['textures']if s['id']==uid)
def sets(stage):return c[stage]['document']['texture_sets']
def content(stage,uid):return next(s for s in sets(stage)if s['id']==uid)
assert native('color',first)['color_hash']!=native('before',first).get('color_hash')
assert native('masked',first)['color_hash']!=native('color',first)['color_hash']
assert native('textured',first)['color_hash']!=native('masked',first)['color_hash']
assert native('projected',first)['color_hash']!=native('textured',first)['color_hash']
for channel in ['color_hash','roughness_hash']:
 assert native('undo',first)[channel]==native('textured',first)[channel]
 assert native('redo',first)[channel]==native('projected',first)[channel]
 assert native('saved',first)[channel]==native('opened',first)[channel]
 assert native('material',first)[channel]==native('material-open',first)[channel]
assert c['masked']['document']==c['invalid']['document'] and c['masked']['undo']==c['invalid']['undo']
assert c['saved']['document']==c['opened']['document']
assert c['material']['document']==c['material-open']['document']
for name,state in c.items():
 assert not state['shader_failures'],name
 assert content(name,other)==content('before',other),name
 image=Image.open(a.output/(name+'-viewport.png')).convert('RGB');assert not any(r>235 and g<20 and b>235 for r,g,b in image.getdata()),name
assert not (a.output/'texture.png').exists() and (a.output/'layers.blend').is_file() and (a.output/'material.blend').is_file()
assert script['deleted_node_rejected'] and script['cross_stack_rejected'] and script['material_source_packed']
proof={'pid':d['pid'],'locale':'sv_SE.UTF-8','stages':list(c),'invalid_edits':5,'layer_events':script['events'],'native_color_mask_texture_projection_changed':True,'undo_redo_exact':True,'packed_texture_open_without_original':True,'native_material_source_packed':True,'other_texture_set_unchanged':True,'deleted_cross_stack_handles_rejected':True,'magenta_pixels':0}
(a.output/'verified.json').write_text(json.dumps(proof,indent=2)+'\n');print(json.dumps(proof,indent=2))
