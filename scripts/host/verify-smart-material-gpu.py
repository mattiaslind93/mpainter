"""Private offscreen smart-material Shelf/drop/undo/packed-project GPU regression."""
import argparse,json,os,re,subprocess
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);p.add_argument('--existing',action='store_true');a=p.parse_args();out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
root=Path(__file__).resolve().parents[2];base=root.parent
assert os.environ.get('QT_QPA_PLATFORM')=='offscreen','An isolated offscreen invocation is required'
cmd=['bash',str(root/'scripts/host/run-host.sh'),'--workspace','--project',str(base/'build/baseline-demo/host-fixture.blend'),'--environment',str(base/'build/environment-cache/TT_environmentLights_v001.hdr'),'--new-mesh',str(root/'native/tests/fixtures/two_materials.obj'),'--new-demo-dir',str(out),'--smart-material-demo',str(base/'build/mesh-maps-native/mesh-maps.blend'),'--library-dir',str(out/'library'),'--recovery-dir',str(out/'recovery')]
if not a.existing:
 with (out/'run.log').open('w') as log:
  proc=subprocess.run(cmd,stdout=log,stderr=subprocess.STDOUT,timeout=300,env={**os.environ,'BTS_SMART_MATERIAL_GPU_CHECK':'1'})
 assert proc.returncode==0,proc.returncode
log=(out/'run.log').read_text();assert 'BTS_SHUTDOWN clean' in log
assert not re.search(r'BTS_GPU_AUTHORING_CPU_READ|BTS_GL_ERROR|BTS_DRAW_FAILED|BTS_FINALIZE_FALLBACK',log)
data=json.loads((out/'paint.json').read_text());captures={c['stage']:c for c in data['captures']}
def stats(stage):return captures[stage]['smart_material']
assert not stats('missing')['ready'] and stats('loaded')['ready']
assert stats('applied')['folders']==1 and stats('selector')['folders']==stats('dragged')['folders']==2
for before,after in [('loaded','undo'),('applied','redo'),('child','undo-child'),('material','undo-selector'),('saved','reopened'),('saved','undo-open'),('saved','undo-drag')]:assert captures[before]['document']==captures[after]['document'],(before,after)
assert captures['edited']['document']!=captures['child']['document'] and captures['edited-open']['document']!=captures['reopened']['document']
assert stats('map-view')['mesh_map_mode']>=0 and stats('mask-view')['mesh_map_mode']==-1 and stats('mask-view')['inspection']==5
first=captures['loaded']
for stage,c in captures.items():
 if stage=='missing':continue
 assert c['document']['texture_sets'][0]==first['document']['texture_sets'][0],stage
 for index in (0,1):assert c['document']['texture_sets'][index]['mesh_maps']==first['document']['texture_sets'][index]['mesh_maps'],stage
 if stage in ('applied','edited','reopened','dragged'):assert c['smart_material_gpu_error']<.01,(stage,c['smart_material_gpu_error'])
 if stage not in ('reopened','edited-open','undo-open','dragged','undo-drag'):assert c['view']['scene_view_matrix']==first['view']['scene_view_matrix'],stage
for stage in ('applied','reopened','dragged'):
 layers=captures[stage]['document']['texture_sets'][1]['layers']
 generators=[e for l in layers for e in (l.get('mask') or {}).get('effects',[]) if e['type']=='generator']
 assert {e['generator'] for e in generators}>={2,3},(stage,generators)
result={'offscreen':True,'builtin_shelf_and_selector':True,'real_asset_drop':True,'atomic_undo_redo':True,'packed_open':True,'live_ao_curvature_generators':True,'gpu_validation':'Native per-component precision-aware oracle; absolute summary < 0.01 accommodates one adjacent sRGB8 code', 'gpu_max_error':max(c['smart_material_gpu_error'] for c in captures.values() if 'smart_material_gpu_error' in c),'scope':'Builtin smart material foundation; generic user presets remain pending. Legacy demo makes explicit CPU diagnostic queries; not a latency benchmark.'}
(out/'verified.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))
