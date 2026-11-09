# SPDX-License-Identifier: GPL-3.0-or-later
"""Native material-channel Compare masks through GPU prefixes, controls and packed reopen."""
import argparse,json,os,re,subprocess
from pathlib import Path
from types import SimpleNamespace
from PIL import Image
p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);p.add_argument('--mesh',type=Path,required=True);p.add_argument('--environment',type=Path,required=True);p.add_argument('--project',type=Path,required=True);a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True)
root=Path(__file__).resolve().parents[2]
command=['bash',str(root/'scripts/host/run-host.sh'),'--workspace','--project',str(root.parent/'build/baseline-demo/host-fixture.blend'),'--environment',str(a.environment),'--new-mesh',str(a.mesh),'--new-demo-dir',str(a.output),'--compare-mask-demo',str(a.project),'--recovery-dir',str(a.output/'recovery')]
with (a.output/'run.log').open('w') as log:
 completed=subprocess.run(command,env=dict(os.environ,BTS_VERIFY_GPU_STACK='1',LANG='sv_SE.UTF-8',LC_ALL='sv_SE.UTF-8'),stdout=log,stderr=subprocess.STDOUT,timeout=240)
assert completed.returncode==0
log=(a.output/'run.log').read_text();demo=json.loads((a.output/'paint.json').read_text());process=SimpleNamespace(pid=demo['pid']);result={}
assert f'BTS_SHUTDOWN clean pid={process.pid}' in log
assert not re.search(r'BTS_HOST_FAILED|BTS_DRAW_FAILED|Shader compilation failed|BTS_FINALIZE_FALLBACK|BTS_GPU_STACK_FALLBACK|BTS_GPU_CPU_FALLBACK',log,re.I)
demo=json.loads((a.output/'paint.json').read_text());assert demo['pid']==process.pid
captures={c['stage']:c for c in demo['captures']}
stages=['initial', 'setup', 'fresh', 'constant-greater', 'less', 'tolerance', 'wide-tolerance', 'identical', 'layer-less', 'below-greater', 'lower-edited', 'undo-lower', 'this-layer', 'current-edited', 'undo-current', 'soft', 'hard', 'mask-prefix', 'prefix-edited', 'undo-prefix', 'saved', 'reopened', 'edited-open', 'undo-open', 'redo-open', 'restored', 'effect-off', 'effect-on'];assert list(captures)==stages
def data(stage):return captures[stage]['compare_mask']
def doc(stage):return captures[stage]['document']['texture_sets'][0]
def effect(stage):return next(e for e in doc(stage)['layers'][-1]['mask']['effects'] if e['type']=='compare')
expected=dict(zip(stages[1:],[.25,0,0,1,0,1,1,1,1,0,1,0,1,0,.5,0,1,0,1,1,1,0,1,0,1,.25,1]))
errors={};sphere=captures['setup']['document']['texture_sets'][1]
for stage,c in list(captures.items())[1:]:
    errors[stage]=abs(data(stage)['value']-expected[stage]);assert errors[stage]<2e-6,(stage,data(stage),expected[stage])
    frame=Image.open(a.output/f'{stage}-viewport.png').convert('RGB');pixel=frame.getpixel((frame.width*3//4,frame.height//2));assert max(abs(component-255*expected[stage]) for component in pixel)<=1,(stage,pixel,expected[stage])
    assert c['document']['texture_sets'][1]==sphere and data(stage)['sphere']==data('setup')['sphere'],stage
    assert not data(stage)['paint_enabled'],stage
    assert doc(stage)['layers'][-1]['effects']==doc('setup')['layers'][-1]['effects'],stage
    for key in ['mesh_objects','uv_triangles','bounds_minimum','bounds_maximum']:assert c['native'][key]==captures['setup']['native'][key],(stage,key)
    if stage!='setup':
        assert data(stage)['histogram_ready'] and all(sum(bins)==256 for bins in data(stage)['histogram_bins']),stage
        assert data(stage)['properties'] and data(stage)['channels']==[0,1,2,3,4] and data(stage)['horizontal_scroll']==0,stage
        assert data(stage)['constant_visible']==(effect(stage)['left']==3 or effect(stage)['right']==3),stage
        assert data(stage)['tolerance_visible']==(effect(stage)['operation']==1),stage
        assert data(stage)['identical_warning']==(effect(stage)['left']==effect(stage)['right']),stage
assert effect('fresh')['channel']==4 and effect('fresh')['left']==1 and effect('fresh')['right']==0 and effect('fresh')['operation']==2
assert abs(effect('fresh')['hardness']-.9)<1e-6 and abs(effect('fresh')['tolerance']-.1)<1e-6
assert len(doc('fresh')['layers'][-1]['mask']['effects'])==1 and doc('fresh')['layers'][-1]['blending'][4]=='Normal'
assert not data('identical')['channel_enabled'] and data('wide-tolerance')['channel_enabled']
assert doc('saved')==doc('reopened')==doc('undo-open')==doc('restored') and doc('edited-open')==doc('redo-open')
for left,right in [('lower-edited','undo-lower'),('this-layer','current-edited'),('mask-prefix','prefix-edited')]:assert data(left)['hash']!=data(right)['hash']
assert (a.output/'compare-mask.blend').is_file()
result['compare_mask_demo']={'pid':process.pid,'stages':stages,'native_scalar_errors':errors,'three_operators_and_identical_operand_warning':True,'live_cross_channel_lower_and_current_content_inputs':True,'preceding_mask_input':True,'atomic_replacement_preserves_content_effects_resets_height_blend':True,'hardness_tolerance_and_dynamic_controls':True,'dependency_cache_reacts_to_both_content_sources':True,'packed_open_further_edit_undo_redo':True,'sphere_and_geometry_unchanged':True,'scope':'PBR/AO operands; own linear luminance and smooth comparison boundaries. Unmeasured legacy boundary/HDR pixel parity is not claimed.'}
checks=re.findall(r'BTS_GPU_STACK_VERIFIED channel=15 probes=64 max_error=(\S+) valid=(\d+)',log)
assert len(checks)>=15 and all(v[1]=='1' for v in checks)
assert 'BTS_GPU_MATERIAL_COMPARE_VERIFIED' in log
result['gpu']={'checks':len(checks),'probes_per_check':64,'max_error':max(float(v[0])for v in checks),'cpu_composition_fallbacks':0}
(a.output/'verified.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
