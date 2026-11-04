"""Native Levels GPU histogram and Auto, controls, Undo and packed reopen."""
import argparse,json,os,re,subprocess
from pathlib import Path
from types import SimpleNamespace
from PIL import Image
p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);p.add_argument('--mesh',type=Path,required=True);p.add_argument('--environment',type=Path,required=True);p.add_argument('--project',type=Path,required=True);a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True)
root=Path(__file__).resolve().parents[2]
command=['bash',str(root/'scripts/host/run-host.sh'),'--workspace','--project',str(root.parent/'build/baseline-demo/host-fixture.blend'),'--environment',str(a.environment),'--new-mesh',str(a.mesh),'--new-demo-dir',str(a.output),'--levels-demo',str(a.project),'--recovery-dir',str(a.output/'recovery')]
with (a.output/'run.log').open('w') as log:
 completed=subprocess.run(command,env=dict(os.environ,BTS_VERIFY_GPU_STACK='1',LANG='sv_SE.UTF-8',LC_ALL='sv_SE.UTF-8'),stdout=log,stderr=subprocess.STDOUT,timeout=240)
assert completed.returncode==0
log=(a.output/'run.log').read_text();demo=json.loads((a.output/'paint.json').read_text());process=SimpleNamespace(pid=demo['pid']);result={}
assert f'BTS_SHUTDOWN clean pid={process.pid}' in log
assert not re.search(r'BTS_HOST_FAILED|BTS_DRAW_FAILED|Shader compilation failed|BTS_FINALIZE_FALLBACK|BTS_GPU_STACK_FALLBACK|BTS_GPU_CPU_FALLBACK',log,re.I)
import math
demo=json.loads((a.output/'paint.json').read_text());assert demo['pid']==process.pid
captures={c['stage']:c for c in demo['captures']}
stages=['initial','setup','fresh','red','green','blue','global','undo-global','redo-global','reset-all','auto','hdr','clamped','reset-base','roughness','rough-invert','rough-clamp','rough-reset','color','rgb-restored','saved','reopened','component-red','component-blue','edited','undo','redo','restored','midtone-handle','undo-handle','channels-removed','undo-channels'];assert list(captures)==stages
base=[.2,.4,.8];complex_color=[.8,.6,math.sqrt(.8)];global_color=[.2,.4,math.sqrt(.2)]
expected={stage:base[:] for stage in stages[1:]};expected.update({'red':[.8,.4,.8],'green':[.8,.6,.8],'blue':complex_color,'global':global_color,'undo-global':complex_color,'redo-global':global_color,'auto':[0,1/3,1],'hdr':[-1,0,2],'clamped':[0,0,1]})
for stage in ['rgb-restored','saved','reopened','component-red','component-blue','undo','restored','undo-handle','channels-removed','undo-channels']:expected[stage]=complex_color
for stage in ['edited','redo','midtone-handle']:expected[stage]=[.8,.6,.8]
def data(stage):return captures[stage]['levels']
def doc(stage):return captures[stage]['document']['texture_sets'][0]
def effect(stage):return doc(stage)['layers'][-1]['effects'][0]
errors={};sphere=captures['setup']['document']['texture_sets'][1];lower=doc('setup')['layers'][:-1]
for stage,c in list(captures.items())[1:]:
    errors[stage]=max(abs(a0-b0) for a0,b0 in zip(data(stage)['samples'][:3],expected[stage]));assert errors[stage]<3e-7,(stage,data(stage)['samples'],expected[stage])
    rough=.75 if stage in ['rough-invert','rough-clamp'] else .25;assert abs(data(stage)['roughness']-rough)<1e-7,stage
    assert data(stage)['samples'][3]==1 and not data(stage)['paint_enabled'],stage
    assert c['document']['texture_sets'][1]==sphere and data(stage)['sphere']==data('setup')['sphere'] and doc(stage)['layers'][:-1]==lower,stage
    assert c['view']['scene_view_matrix']==captures['setup']['view']['scene_view_matrix'],stage
    for key in ['mesh_objects','uv_triangles','bounds_minimum','bounds_maximum']:assert c['native'][key]==captures['setup']['native'][key],(stage,key)
    if stage!='setup':
        assert data(stage)['histogram_ready'],stage
        assert sum(data(stage)['histogram_bins'][0])==(256 if data(stage)['component'] else 768),stage
        assert len(doc(stage)['layers'][-1]['effects'])==1 and effect(stage)['id']==effect('fresh')['id'],stage
        assert data(stage)['horizontal_scroll']==0 and data(stage)['clamp_visible'],stage
        if effect(stage)['channel']==2:assert data(stage)['components']==['Lum'],stage
        else:assert data(stage)['components']==['Color','Red','Green','Blue'],stage
        assert data(stage)['channels']==([0,2,3] if stage=='channels-removed' else [0,1,2,3,4]),stage
assert [effect('fresh')[k] for k in ['input_low','gamma','input_high','output_low','output_high','clamp']]==[0,1,1,0,1,False]
assert effect('rough-reset')['channel']==2 and not effect('rough-reset')['clamp']
assert abs(effect('auto')['input_low']-.2)<1e-7 and abs(effect('auto')['input_high']-.8)<1e-7
assert effect('green')['rgb_curves'][0]['output_low']==1 and effect('green')['rgb_curves'][1]['output_low']==1
assert effect('blue')['rgb_curves'][2]['gamma']==2
assert doc('global')==doc('redo-global') and data('blue')['hash']==data('undo-global')['hash']
assert doc('saved')==doc('reopened')==doc('component-blue')==doc('undo')==doc('restored')==doc('undo-handle')==doc('undo-channels')
assert doc('edited')==doc('redo')==doc('midtone-handle')
assert data('reopened')['undo_count']==data('component-red')['undo_count']==data('component-blue')['undo_count']==0
assert data('component-red')['hash']==data('component-blue')['hash']==data('reopened')['hash']
result['levels_demo']={'pid':process.pid,'stages':stages,'native_rgb_errors':errors,'independent_rgb_curves_survive_editor_component_changes_and_open':True,'global_curve_runs_before_component_curves':True,'component_selection_does_not_create_undo_entries':True,'auto_input_range_and_signed_hdr_clamp':True,'same_effect_retargets_roughness_lum_and_reset_keeps_channel':True,'actual_midtone_handle_and_undo':True,'channel_choices_follow_active_set_channels':True,'compact_controls_clamp_visible_no_horizontal_scroll':True,'packed_open_further_edit_undo_redo':True,'sphere_lower_layers_geometry_camera_unchanged':True,'scope':'Observed defaults/channel/invert/clamp/reset follow the reference. RGB curves compose in linear working values; Auto uses the sampled input range. Unmeasured legacy RGB/gamma/Auto pixel parity is not claimed.'}
assert 'BTS_GPU_HISTOGRAM_VERIFIED' in log
(a.output/'verified.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
