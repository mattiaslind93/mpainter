"""Private native Blender images -> shared Shelf preset -> GPU fill / packed Open."""
import argparse,hashlib,json,os,re,subprocess
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);p.add_argument('--existing',action='store_true');p.add_argument('--image-size',type=int,default=16);a=p.parse_args();out=a.output.resolve();root=Path(__file__).resolve().parents[2];bts=Path(os.environ.get('BTS_ROOT',root.parent));assert out.is_relative_to(bts.resolve()/'build');out.mkdir(parents=True,exist_ok=True);fixtures=out/'fixtures'
if not a.existing:
 with(out/'fixtures.log').open('w')as log:subprocess.run([str(bts/'build/baseline/bin/blender'),'--background','--factory-startup','--python',str(root/'scripts/host/generate-blend-images-fixture.py'),'--',str(fixtures),str(a.image_size)],stdout=log,stderr=subprocess.STDOUT,check=True,timeout=90)
source=fixtures/'materials.blend';before=hashlib.sha256(source.read_bytes()).hexdigest();fingerprint=out/'source-before.sha256'
if not a.existing:fingerprint.write_text(before+'\n')
else:before=fingerprint.read_text().strip()
env=dict(os.environ,LANG='sv_SE.UTF-8',LC_ALL='sv_SE.UTF-8',QT_QPA_PLATFORM='offscreen',BTS_HEADLESS_WORKSPACE='1',BTS_BLEND_MATERIAL_DEMO='1',BTS_BLEND_IMAGES_DEMO='1',BTS_BLEND_MATERIAL_SOURCE=str(source),BTS_SKEW_DEMO='1',BTS_PERFORMANCE_PROFILE='1')
for key,folder in [('XDG_CONFIG_HOME','config'),('XDG_DATA_HOME','data'),('XDG_CACHE_HOME','cache'),('TMPDIR','tmp'),('BTS_RESOURCE_CACHE','resources'),('BTS_RECOVERY_DIR','recovery')]:
 (out/folder).mkdir(exist_ok=True);env[key]=str(out/folder)
try:
 if not a.existing:
  with(out/'run.log').open('w')as log:subprocess.run(['bash',str(root/'scripts/host/run-host.sh'),'--workspace','--gui-fix-demo',str(out),'--gui-fix-mesh',str(root/'native/tests/fixtures/two_tile_udim.obj'),'--gui-fix-resolution','128','--autosave-seconds','3600','--library-dir',str(out/'library')],env=env,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=300)
finally:
 if Path(str(source)+'.held').exists():Path(str(source)+'.held').rename(source)
 if (fixtures/'images.held').exists()and not(fixtures/'images').exists():(fixtures/'images.held').rename(fixtures/'images')
 if (out/'resources.held').exists():
  if(out/'resources').exists():(out/'resources').rename(out/'resources.partial')
  (out/'resources.held').rename(out/'resources')
d=json.loads((out/'blend-images.json').read_text());c={v['stage']:v for v in d['captures']};assert c['shelf-imported']['document']==c['before-import']['document'];assert c['undone']['document']==c['before-import']['document'];assert c['redone']['document']==c['fill-applied']['document'];assert c['undo-replace']['document']==c['before-replace']['document'];assert c['redo-replace']['document']==c['replaced']['document']==c['opened-without-sources']['document'];assert c['undo-reused']['document']==c['opened-without-sources']['document']==c['unsupported-unchanged']['document']==c['missing-tile-unchanged']['document']==c['colored-scalar-unchanged']['document']==c['cancelled-unchanged']['document']
assets=c['shelf-imported']['blend_materials'];assert len(assets)==1 and assets[0]['scope']=='session' and assets[0]['descriptor']['version']==2
assert all(image is None or image['width']==a.image_size and image['height']==a.image_size for image in assets[0]['material_textures']['images']),'Fixture dimensions differ from the requested source size'
fill=c['fill-applied']['document']['texture_sets'][0]['layers'][-1];assert fill['channels']==[True,True,True,False,False]and len(c['fill-applied']['document']['texture_sets'][0]['tiles'])==2
matrix=[[.6130974024,.3395231462,.0473794514],[.0701937225,.9163538791,.0134523985],[.0206155929,.1095697729,.8698146342]];colors={1001:[.8,.1,.2],1002:[.1,.7,.3]}
for name in ['fill-applied','redone','replaced','redo-replace','opened-without-sources','reused-after-open','undo-reused']:
 case=c[name];assert case['shared_scalar_pixels'],name;assert len(case['image_resources'])==2,(name,case['image_resources']);assert sum(len(v['texture_files'])for v in case['image_resources'])==3
 for tile in case['image_tiles']:
  expected=[sum(row[i]*colors[tile['tile']][i]for i in range(3))for row in matrix];samples=tile['samples'];assert all(samples),(name,tile)
  assert max(abs(x-y)for x,y in zip(samples[0][:3],expected))<2e-4,(name,tile,expected)
  assert abs(samples[1][0]-.2)<2e-6 and abs(samples[2][0]-.2)<2e-6,(name,tile)
  if name in ['replaced','redo-replace','opened-without-sources','reused-after-open','undo-reused']:assert abs(samples[3][0]-.125)<2e-6,(name,tile)
project=[v for v in c['opened-without-sources']['blend_materials']if v['scope']=='project'];assert len(project)==1 and project[0]['material_textures']==c['replaced']['blend_materials'][-1]['material_textures']
assert c['cancelled-unchanged']['blend_materials']==c['unsupported-unchanged']['blend_materials'];assert d['max_import_ping_ms']<100,d['max_import_ping_ms'];assert not(out/'library/library.json').exists();assert not(fixtures/'images/gray.png').exists();assert hashlib.sha256(source.read_bytes()).hexdigest()==before
assert c['before-import']['cpu_materializations']==c['cancelled-unchanged']['cpu_materializations'],'Material import materialized paint recipes'
log=(out/'run.log').read_text();allocations=re.findall(r'BTS_GPU_RESOURCE_ALLOC key=(.+?) bytes=(\d+) count=(\d+)',log);assert len(allocations)==6 and [int(x[2])for x in allocations]==[1,2,3,1,2,3],allocations
assert sum(':raw:'in x[0]for x in allocations)==2,'Shared scalar image was allocated twice per scene'
assert 'BTS_SHUTDOWN clean'in log;assert not re.search(r'BTS_HOST_FAILED|BTS_DRAW_FAILED|Shader compilation failed|BTS_BRUSH_CPU_FALLBACK|BTS_GPU_AUTHORING_CPU_READ',log,re.I)
result={'native_same_process_worker':True,'opaque_color_acescg_raw_scalar_alpha_zero':True,'udim_tiles':[1001,1002],'source_image_size':a.image_size,'source_images':2,'gpu_allocations_per_scene':3,'raw_gpu_allocations_per_scene':1,'packed_original_files':3,'shared_raw_pixels':True,'shelf_reuse_after_open':True,'bump_preserved_on_replacement':True,'undo_redo':True,'cancel_and_unsupported_atomic':True,'missing_declared_tile_rejected':True,'colored_scalar_rejected':True,'source_unchanged':True,'max_import_ping_ms':d['max_import_ping_ms'],'no_default_library_write':True,'captures':list(c)};(out/'verified.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
