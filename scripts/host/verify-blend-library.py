"""Native .blend object/collection New, packed Open, reimport and picker cancellation."""
from parity_test_environment import private_display
import argparse,hashlib,json,os,re,subprocess
from pathlib import Path
from PIL import Image,ImageStat

p=argparse.ArgumentParser();p.add_argument('--output',required=True,type=Path)
a=p.parse_args();a.output=a.output.resolve();a.output.mkdir(parents=True,exist_ok=True)
root=Path(__file__).resolve().parents[2];bts=Path(os.environ.get('BTS_ROOT',root.parent));fixtures=a.output/'fixtures'
baseline=bts/'build/baseline/bin/blender'
with (a.output/'fixtures.log').open('w')as log:
 subprocess.run([str(baseline),'--background','--factory-startup','--python',str(root/'scripts/host/generate-blend-library-fixture.py'),'--',str(fixtures)],stdout=log,stderr=subprocess.STDOUT,check=True,timeout=90)
source=fixtures/'library.blend';before=hashlib.sha256(source.read_bytes()).hexdigest()
env=dict(os.environ,LANG='sv_SE.UTF-8',LC_ALL='sv_SE.UTF-8',QT_QPA_PLATFORM='offscreen',BTS_HEADLESS_WORKSPACE='1',DISPLAY=private_display(),XDG_RUNTIME_DIR='/run/user/1000',BTS_BLEND_IMPORT_DEMO='1',BTS_SKEW_DEMO='1',XDG_CONFIG_HOME=str(a.output/'config'),XDG_DATA_HOME=str(a.output/'data'),XDG_CACHE_HOME=str(a.output/'cache'),BTS_RECOVERY_DIR=str(a.output/'recovery'),BTS_RESOURCE_CACHE=str(a.output/'resources'),BTS_SESSION_LOG_DIR=str(a.output/'logs'),BTS_TEMPLATE_DIR=str(a.output/'templates'))
try:
 with (a.output/'run.log').open('w')as log:
  subprocess.run(['bash',str(root/'scripts/host/run-host.sh'),'--workspace','--gui-fix-demo',str(a.output),'--gui-fix-mesh',str(source),'--gui-fix-resolution','128','--autosave-seconds','3600','--library-dir',str(a.output/'shelf')],env=env,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=300)
finally:
 held=Path(str(source)+'.held')
 if held.exists():held.rename(source)
d=json.loads((a.output/'blend-library.json').read_text());c={x['stage']:x for x in d['captures']}
assert set(c)=={'collection-and-object','layer-added','opened','reimport-object','reimport-opened','cancel-preserved'}
for name,s in c.items():
 assert s['has_document']and not s['shader_failures']and s['decimal']=='.'
 objects=s['geometry']['objects'];assert len(objects)==(2 if name in ['collection-and-object','layer-added','opened']else 1),objects
 assert all(x['source_vertices']==8 and x['evaluated_vertices']==8 and x['preview_modifiers']==0 for x in objects),objects
 assert all(not x['object'].startswith('Not selected')for x in objects)
 image=Image.open(a.output/(name+'.png')).convert('RGB');assert sum(ImageStat.Stat(image).mean)>10 and max(ImageStat.Stat(image).stddev)>2
sets=c['collection-and-object']['document']['texture_sets'];assert len(sets)==2
assert sorted(x['tiles']for x in sets)==[[1001],[1002]]
assert c['collection-and-object']['document']['blend_selection']=={'objects':['Body'],'collections':['Character']}
assert c['layer-added']['document']==c['opened']['document']
assert c['layer-added']['source_obj_hash']==c['opened']['source_obj_hash']
assert c['reimport-object']['document']==c['reimport-opened']['document']==c['cancel-preserved']['document']
assert c['reimport-object']['source_obj_hash']==c['reimport-opened']['source_obj_hash']==c['cancel-preserved']['source_obj_hash']
assert c['reimport-object']['document']['blend_selection']=={'objects':['Body'],'collections':[]}
before_sets={x['id']:x for x in c['layer-added']['document']['texture_sets']}
after_sets={x['id']:x for x in c['reimport-object']['document']['texture_sets']}
assert before_sets.keys()==after_sets.keys()
for key,old in before_sets.items():assert [x['id']for x in old['layers']]==[x['id']for x in after_sets[key]['layers']]
assert sum(x['disabled']for x in after_sets.values())==1
assert hashlib.sha256(source.read_bytes()).hexdigest()==before
inspect=a.output/'inspect.py';inspect.write_text('''import bpy
objects=[o for o in bpy.context.scene.objects if o.type=='MESH'];assert len(objects)==1
body=objects[0];assert body.name.startswith('Body')and len(body.data.vertices)==8
assert len(body.data.uv_layers)==1 and abs(body.matrix_world.translation.x-1)<1e-5
assert not body.modifiers and body.parent is None and body.animation_data is None and body.data.shape_keys is None
assert not bpy.data.libraries
print('BTS_BLEND_APPEND_STOCK_OPEN_OK')
''')
with (a.output/'stock-open.log').open('w')as log:
 subprocess.run([str(baseline),'--background',str(a.output/'skew.blend'),'--python',str(inspect)],stdout=log,stderr=subprocess.STDOUT,check=True,timeout=90)
assert 'BTS_BLEND_APPEND_STOCK_OPEN_OK'in (a.output/'stock-open.log').read_text()
log=(a.output/'run.log').read_text();assert f'BTS_GPU_READY pid={d["pid"]}'in log and f'BTS_SHUTDOWN clean pid={d["pid"]}'in log
assert not re.search(r'BTS_DRAW_FAILED|BTS_HOST_FAILED|Shader compilation failed|BTS_GPU_AUTHORING_CPU_READ|BTS_GPU_STACK_FALLBACK',log,re.I)
result={'pid':d['pid'],'native_append_same_process':True,'collection_and_object_multiselect_deduplicated':True,'nested_collection_and_unicode_name':True,'unselected_mesh_excluded':True,'original_cages_and_uv_tiles':True,'single_non1001_texture_set_roundtrip':True,'source_modifiers_and_shape_keys_omitted':True,'frame_1_parent_transform_baked':True,'packed_open_without_source_blend':True,'reimport_selection_and_layer_ids_preserved':True,'unmatched_stack_disabled_retained':True,'cancel_preserves_document_and_geometry':True,'source_unchanged_sha256':before,'stock_blender_open_without_linked_libraries':True,'scope':'Static original mesh cages, native working-space append, modifiers/animation omitted; collection-instance objects require selecting their source collection.'}
(a.output/'verified.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
