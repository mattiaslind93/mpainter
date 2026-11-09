# SPDX-License-Identifier: GPL-3.0-or-later
"""Real content/mask Paint effect gestures with GPU prefixes and packed reopen."""
import argparse,json,os,re,subprocess
from pathlib import Path
from types import SimpleNamespace
from PIL import Image,ImageChops,ImageStat
p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);p.add_argument('--mesh',type=Path);p.add_argument('--environment',type=Path);p.add_argument('--project',type=Path);p.add_argument('--existing',action='store_true');a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True)
root=Path(__file__).resolve().parents[2]
if not a.existing:
 assert a.mesh and a.environment and a.project
 command=['bash',str(root/'scripts/host/run-host.sh'),'--workspace','--project',str(root.parent/'build/baseline-demo/host-fixture.blend'),'--environment',str(a.environment),'--new-mesh',str(a.mesh),'--new-demo-dir',str(a.output),'--paint-effect-demo',str(a.project)]
 with (a.output/'run.log').open('w') as log:
  completed=subprocess.run(command,env=dict(os.environ,BTS_VERIFY_GPU_STACK='1',LANG='sv_SE.UTF-8',LC_ALL='sv_SE.UTF-8'),stdout=log,stderr=subprocess.STDOUT,timeout=240)
 assert completed.returncode==0
log=(a.output/'run.log').read_text();demo=json.loads((a.output/'paint.json').read_text());process=SimpleNamespace(pid=demo['pid']);result={}
assert f'BTS_SHUTDOWN clean pid={process.pid}' in log
assert not re.search(r'BTS_HOST_FAILED|BTS_DRAW_FAILED|Shader compilation failed|BTS_FINALIZE_FALLBACK',log,re.I)
actual=re.findall(r'BTS_GPU_STACK_VERIFIED channel=(\d+) probes=64 max_error=(\S+) valid=(\d+)',log)
assert len(actual)>100 and all(row[2]=='1' for row in actual)
assert 'BTS_GPU_CONTENT_PAINT_VERIFIED' in log
assert 'BTS_BRUSH_GPU_RECORD' in log
assert 'BTS_BRUSH_CPU_FALLBACK' not in log
demo=json.loads((a.output/'paint.json').read_text());assert demo['pid']==process.pid
captures={c['stage']:c for c in demo['captures']};stages=['initial', 'setup', 'mask-empty', 'mask-stroke', 'undo-mask', 'redo-mask', 'mask-off', 'mask-on', 'below-compare', 'undo-order', 'mask50', 'mask100', 'content-empty', 'content-stroke', 'undo-content', 'redo-content', 'content50', 'content100', 'multiply', 'normal', 'saved', 'reopened', 'paint-open', 'undo-open', 'redo-open', 'erase-open', 'undo-erase', 'cancelled', 'mask-inspected', 'paint3d', 'undo3d'];assert list(captures)==stages
def data(stage):return captures[stage]['paint_effect']
def doc(stage):return captures[stage]['document']['texture_sets'][0]
def top(stage):return doc(stage)['layers'][-1]
def maskfx(stage):return top(stage)['mask']['effects']
def contentfx(stage):return top(stage)['effects']
base=[.8,.6,.2];ink=[1,.1,.05];errors={};sphere=captures['setup']['document']['texture_sets'][1]
white=['setup','mask-empty','undo-mask','mask-off','below-compare'];color_stages=['content-stroke','redo-content','content100','normal','saved','reopened','paint-open','undo-open','redo-open','undo-erase','cancelled','mask-inspected','paint3d','undo3d']
for stage,c in list(captures.items())[1:]:
    value=1 if stage in white else .5 if stage=='mask50' else 0;assert abs(data(stage)['mask_value']-value)<1e-6,(stage,data(stage)['mask_value'],value)
    assert (stage=='paint3d' or data(stage)['mask_other']==1) and data(stage)['parent_ink']==data('setup')['parent_ink'] and data(stage)['parent_mask_ink']==data('setup')['parent_mask_ink'],stage
    expected=ink if stage in color_stages else [(a0+b0)/2 for a0,b0 in zip(base,ink)] if stage=='content50' else [a0*b0 for a0,b0 in zip(base,ink)] if stage=='multiply' else base
    errors[stage]=max(abs(a0-b0) for a0,b0 in zip(data(stage)['color'][:3],expected));assert errors[stage]<3e-7,(stage,data(stage)['color'],expected)
    assert c['document']['texture_sets'][1]==sphere and data(stage)['sphere']==data('setup')['sphere'],stage
    assert len(doc(stage)['layers'])==len(doc('setup')['layers']) and contentfx(stage)[0]==contentfx('setup')[0],stage
    assert data(stage)['horizontal_scroll']==0,stage
    if stage!='setup':assert data(stage)['paint_enabled'] and data(stage)['mix_visible'],stage
    for key in ['mesh_objects','uv_triangles','bounds_minimum','bounds_maximum']:assert c['native'][key]==captures['setup']['native'][key],(stage,key)
assert len(maskfx('mask-empty'))==2 and maskfx('mask-empty')[-1]['type']=='paint' and maskfx('mask-empty')[0]['type']=='compare'
assert len(contentfx('content-empty'))==2 and contentfx('content-empty')[-1]['type']=='paint' and len(maskfx('content-empty'))==2
for stage in stages[2:12]:assert data(stage)['gray_visible'],stage
for stage in stages[12:28]:assert not data(stage)['gray_visible'],stage
assert doc('mask-stroke')==doc('redo-mask')==doc('mask-on')==doc('undo-order')
assert doc('content-stroke')==doc('redo-content')==doc('content100')==doc('normal')==doc('saved')==doc('reopened')==doc('undo-open')
assert doc('paint-open')==doc('redo-open')==doc('undo-erase')==doc('cancelled')
assert data('mask-stroke')['mask_hash']!=data('undo-mask')['mask_hash'] and data('content-stroke')['color_hash']!=data('undo-content')['color_hash']
assert data('paint3d')['mask_ink']!=data('mask-inspected')['mask_ink'] and data('undo3d')['mask_ink']==data('mask-inspected')['mask_ink']
assert data('paint-open')['low'][0]>.99 and data('undo-open')['low'][0]<.9
assert data('paint-open')['mask_ink']==data('reopened')['mask_ink']
assert (a.output/'paint-effects.blend').is_file()
result['paint_effect_demo']={'pid':process.pid,'stages':stages,'native_color_errors':errors,'independent_sparse_mask_and_content_contributions':True,'actual_uv_and_3d_strokes':True,'stroke_undo_redo_and_cancel':True,'effect_eye_opacity_and_blending':True,'alt_arrow_order_over_compare_mask':True,'parent_base_rasters_unchanged':True,'mask_and_content_effect_chains_preserved_separately':True,'packed_open_further_edit_erase_undo':True,'sphere_and_geometry_unchanged':True,'scope':'Effect paint remains UV-bound during UV reprojection. Session Undo retains entire strokes; gesture replay for effect rasters is not implemented.'}
result['native_gpu']={'checks':len(actual),'probes_per_check':64,'max_error':max(float(row[1]) for row in actual),'gpu_brush_batches':log.count('BTS_BRUSH_GPU_RECORD'),'production_readbacks':0}
(a.output/'content-paint-gpu-verified.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
