"""Offscreen smart-mask creation, sparse merge, Ctrl-replace, undo and packed reopen."""
import argparse,json,os,subprocess,re
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);p.add_argument('--existing',action='store_true');p.add_argument('--bitmap',action='store_true');a=p.parse_args();out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
root=Path(__file__).resolve().parents[2];base=root.parent
assert os.environ.get('QT_QPA_PLATFORM')=='offscreen','Use an isolated offscreen invocation'
cmd=['bash',str(root/'scripts/host/run-host.sh'),'--workspace','--project',str(base/'build/baseline-demo/host-fixture.blend'),'--environment',str(base/'build/environment-cache/TT_environmentLights_v001.hdr'),'--new-mesh',str(root/'replica/specs/fixtures/two_materials.obj'),'--new-demo-dir',str(out),'--smart-mask-demo',str(base/'build/mesh-maps-native/mesh-maps.blend'),'--library-dir',str(out/'library'),'--recovery-dir',str(out/'recovery')]
if not a.existing:
 with (out/'run.log').open('w') as log:
  proc=subprocess.run(cmd,stdout=log,stderr=subprocess.STDOUT,timeout=240,env={**os.environ,**({'BTS_SMART_MASK_BITMAP':'1'} if a.bitmap else {})})
 assert proc.returncode==0,proc.returncode
log=(out/'run.log').read_text();assert 'BTS_SHUTDOWN clean' in log
assert not re.search(r'BTS_GPU_AUTHORING_CPU_READ|BTS_GL_ERROR|BTS_DRAW_FAILED|BTS_FINALIZE_FALLBACK',log)
data=json.loads((out/'paint.json').read_text());pids=set(re.findall(r'BTS_(?:SCENE_READY|GPU_READY|NEW_PROJECT|FRAME) pid=(\d+)',log));assert pids=={str(data['pid'])},pids
captures={c['stage']:c for c in data['captures']}
def info(stage):return captures[stage]['smart_mask']
def doc(stage):return captures[stage]['document']['texture_sets'][0]
def layer(stage,key):return next(l for l in doc(stage)['layers'] if l['id']==info(stage)[key].strip('{}'))
asset=info('preset-saved')['asset'];assert asset['name']=='Vertical mask fixture'
assert asset['paint_hash']==info('painted')['mask_hash']
for before,after in [('destination','undo-merge'),('merged','redo-merge'),('merged','undo-replace'),('ctrl-replaced','redo-replace'),('without-mask','undo-create'),('created','redo-create'),('saved','reopened'),('saved','undo-paint')]:assert doc(before)==doc(after),(before,after)
source_effects=layer('painted','source')['mask']['effects'];destination_effects=layer('destination','target')['mask']['effects']
effects=layer('merged','target')['mask']['effects'];assert len(effects)==len(destination_effects)+1+len(source_effects) and effects[len(destination_effects)]['type']=='paint'
assert len({e['id'] for e in effects})==len(effects)
if a.bitmap:
 assert any(e.get('mapping',{}).get('texture') for e in source_effects)
 assert any(e.get('mapping',{}).get('texture') for e in layer('reopened','empty')['mask']['effects'])
 assert captures['reopened']['document']['schema']>=65
source_hash=info('painted')['native_hash']
assert info('merged')['native_hash'] not in (source_hash,info('destination')['native_hash'])
for stage in ('ctrl-replaced','redo-replace','created','redo-create','saved','reopened','undo-paint'):assert info(stage)['native_hash']==source_hash,stage
assert info('paint-open')['native_hash']!=source_hash
assert info('paint-open')['native_hash']==info('redo-paint')['native_hash']
assert layer('created','empty')['mask']['background']==0
library=json.loads((out/'library/library.json').read_text());assert any(r['name']==asset['name'] and r['sha256']==asset['hash'] for r in library['resources'])
bank_proof=None
if a.bitmap:
 stock=out/'inspect-mask-bank.py'
 stock.write_text("import bpy,json\nfrom pathlib import Path\nr=json.loads(bpy.context.scene['bts_resources'])\nbanks=[a for a in r['resources'] if 'internal:smart-mask-textures' in a.get('tags',[])]\nassert len(banks)==1,banks\nPath("+repr(str(out/'packed-bank.json'))+").write_text(json.dumps({'source_banks':len(banks),'name':banks[0]['name'],'sha256':banks[0]['sha256']}))\n")
 with (out/'stock-resource-bank.log').open('w') as stock_log:
  code=subprocess.run([str(base/'build/baseline/bin/blender'),'--factory-startup','--background',str(out/'smart-mask.blend'),'--disable-autoexec','--python',str(stock)],stdout=stock_log,stderr=subprocess.STDOUT,env={**os.environ,'LD_LIBRARY_PATH':str(base/'build/baseline/bin/lib')},timeout=120).returncode
 assert code==0,code
 bank_proof=json.loads((out/'packed-bank.json').read_text())
result={'single_process':True,'bitmap_resources':a.bitmap,'packed_source_bank':bank_proof,'offscreen':True,'saved_gpu_mask_matches_authored_pixels':True,'merge_uses_paint_stage':True,'ctrl_replace_exact':True,'undo_redo_exact':True,'packed_open_exact':True,'user_library_persisted':True,'gl_or_gpu_cpu_read_errors':0,'diagnostic_cpu_reference_count':log.count('BTS_GPU_CPU_REFERENCE'),'not_a_latency_benchmark':True,'reference_scope':'Legacy demo explicitly queries CPU reference pixels between stages; production preset capture and merge are separately GPU-only guarded. Run the existing AA performance gate for latency.','stages':list(captures)}
(out/'verified.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
