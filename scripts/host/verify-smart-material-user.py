"""Private native folder capture, portable preset import/export, reuse and project roundtrip."""
import argparse,json,os,re,struct,subprocess,zlib
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);p.add_argument('--existing',action='store_true');a=p.parse_args();out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
root=Path(__file__).resolve().parents[2];base=root.parent
assert os.environ.get('QT_QPA_PLATFORM')=='offscreen','Use an isolated offscreen invocation'
image=out/'smart-material-source.png'
if not a.existing:
 def chunk(kind,data):return struct.pack('!I',len(data))+kind+data+struct.pack('!I',zlib.crc32(kind+data)&0xffffffff)
 raw=b''.join(b'\0'+bytes(c for x in range(32) for c in (40+x*6,30+y*6,180 if (x//8+y//8)%2 else 60)) for y in range(32))
 image.write_bytes(b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('!2I5B',32,32,8,2,0,0,0))+chunk(b'IDAT',zlib.compress(raw))+chunk(b'IEND',b''))
 # This is our generated export fixture, never an artist-owned file.
 (out/'own-material.btsmaterial').unlink(missing_ok=True)
 cmd=['bash',str(root/'scripts/host/run-host.sh'),'--workspace','--project',str(base/'build/baseline-demo/host-fixture.blend'),'--environment',str(base/'build/environment-cache/TT_environmentLights_v001.hdr'),'--new-mesh',str(root/'replica/specs/fixtures/two_materials.obj'),'--new-demo-dir',str(out),'--smart-material-demo',str(base/'build/mesh-maps-native/mesh-maps.blend'),'--library-dir',str(out/'library'),'--recovery-dir',str(out/'recovery')]
 with (out/'run.log').open('w') as log:
  proc=subprocess.run(cmd,stdout=log,stderr=subprocess.STDOUT,timeout=420,env={**os.environ,'BTS_SMART_MATERIAL_GPU_CHECK':'1','BTS_SMART_MATERIAL_USER':'1','BTS_SMART_MATERIAL_SOURCE':str(image)})
 assert proc.returncode==0,proc.returncode
log=(out/'run.log').read_text();assert 'BTS_SHUTDOWN clean' in log
assert not re.search(r'BTS_GPU_AUTHORING_CPU_READ|BTS_GL_ERROR|BTS_DRAW_FAILED|BTS_FINALIZE_FALLBACK|BTS_FEATURE_DEMO_FAILED',log)
assert re.search(r'BTS_SMART_MATERIAL_PRESET_SAVED cpu_materializations=0\b',log),'Preset capture evaluated a brush recipe on the CPU'
data=json.loads((out/'paint.json').read_text());captures={c['stage']:c for c in data['captures']}
assert len(captures)==37,len(captures)
def values(stage):
 samples=captures[stage]['user_gpu_samples'];assert len(samples)==6,(stage,samples)
 return [v for sample in samples for v in sample]
def equal(a,b):
 x,y=values(a),values(b);error=max(abs(u-v) for u,v in zip(x,y));assert error<1e-6,(a,b,error)
for stage in ('user-applied','user-redo','user-saved','user-reopened','user-child-undo','user-drop-undo','user-selector-undo'):equal('user-created',stage)
assert captures['user-undo']['user_gpu_samples']!=captures['user-applied']['user_gpu_samples']
assert values('user-child-edited')!=values('user-reopened')
for stage in ('user-created','user-applied','user-reopened'):assert captures[stage]['smart_material_gpu_error']<.01
for before,after in [('loaded','undo'),('applied','redo'),('child','undo-child'),('material','undo-selector'),('saved','reopened'),('saved','undo-open'),('saved','undo-drag'),('user-saved','user-reopened')]:assert captures[before]['document']==captures[after]['document'],(before,after)
def folder_count(stage):return sum(l['kind']=='folder' and l['name']=='Imported metal fixture' for l in captures[stage]['document']['texture_sets'][1]['layers'])
assert folder_count('user-applied')==1 and folder_count('user-undo')==0
assert folder_count('user-dropped')==folder_count('user-selector')==2
assert folder_count('user-drop-undo')==folder_count('user-selector-undo')==1
assert captures['user-no-maps']['document']==captures['user-missing-maps']['document']
assert 'Bake Mesh Maps' in captures['user-missing-maps']['user_error'] and 'Bake Mesh Maps' in captures['user-missing-maps']['user_status']
assets=[x for x in captures['user-imported']['user_materials'] if x['scope']=='user'];assert len(assets)==2,assets
assert {x['name'] for x in assets}=={'Own metal fixture','Imported metal fixture'}
assert assets[0]['hash']==assets[1]['hash'] and all(x['paint_pages']>0 for x in assets)
assert assets[0]['texture_storage'] and assets[0]['texture_storage']==assets[1]['texture_storage'],'Same image decoded into different pixel allocations'
assert not image.exists() and Path(str(image)+'.unavailable').exists(),'Original image was not retired before portable reuse'
library=json.loads((out/'library/library.json').read_text());assert len([x for x in library['resources'] if x['usages']==['smartmaterial']])==2
assert len(list((out/'library/blobs').iterdir()))==1,'Identical exported/imported archives should use one library blob'
stock=out/'inspect-material-bank.py';stock.write_text("import bpy,json\nfrom pathlib import Path\nr=json.loads(bpy.context.scene['bts_resources'])\nbanks=[a for a in r['resources'] if 'internal:smart-mask-textures' in a.get('tags',[])]\nassert len(banks)==1,banks\nassert not any(a['usages']==['smartmaterial'] for a in r['resources'])\nPath("+repr(str(out/'packed-bank.json'))+").write_text(json.dumps({'source_banks':len(banks),'resources':len(r['resources'])}))\n")
with (out/'stock-resource-bank.log').open('w') as log_file:
 code=subprocess.run([str(base/'build/baseline/bin/blender'),'--factory-startup','--background',str(out/'user-material.blend'),'--disable-autoexec','--python',str(stock)],stdout=log_file,stderr=subprocess.STDOUT,timeout=120,env={**os.environ,'LD_LIBRARY_PATH':str(base/'build/baseline/bin/lib')}).returncode
assert code==0,code
result={'offscreen':True,'folder_context_creation':True,'gpu_painted_source':True,'cpu_recipe_materializations_during_capture':0,'portable_export_import':True,'shelf_and_selector_and_real_drop':True,'independent_parameters':True,'undo_redo':True,'packed_open_without_source_image':True,'shared_decoded_texture_storage':True,'user_library_blob_count':1,'packed_source_bank':json.loads((out/'packed-bank.json').read_text()),'gpu_max_error':max(c.get('smart_material_gpu_error',0) for c in captures.values()),'scope':'Legacy native demo uses explicit CPU diagnostic queries between actions; not a latency benchmark. Creation/readback itself is guarded and must report zero CPU recipe evaluations.'}
(out/'user-verified.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
