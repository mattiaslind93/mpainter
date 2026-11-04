"""Multichannel material projection gestures, GPU authoring and packed roundtrip."""
import argparse,json,os,re,subprocess,signal
from pathlib import Path
from types import SimpleNamespace
p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);p.add_argument('--environment',type=Path);p.add_argument('--existing',action='store_true');a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True)
root=Path(__file__).resolve().parents[2];build=root.parent/'build'
if not a.existing:
 assert a.environment, "--environment must name the NewProject fixture HDRI"
 fixture=build/'quick-mask-native/material-projection.obj'
 command=['bash',str(root/'scripts/host/run-host.sh'),'--workspace','--project',str(build/'baseline-demo/host-fixture.blend'),'--environment',str(a.environment),'--new-mesh',str(root/'fixtures/single_tile.obj'),'--new-demo-dir',str(a.output),'--material-projection-demo',str(fixture),'--recovery-dir',str(a.output/'recovery')]
 with (a.output/'run.log').open('w') as log:
  child=subprocess.Popen(command,env=dict(os.environ,BTS_VERIFY_GPU_STACK='1',LANG='sv_SE.UTF-8',LC_ALL='sv_SE.UTF-8'),stdout=log,stderr=subprocess.STDOUT,start_new_session=True)
  try:
   code=child.wait(timeout=240)
  except subprocess.TimeoutExpired:
   os.killpg(child.pid,signal.SIGTERM)
   try:child.wait(timeout=5)
   except subprocess.TimeoutExpired:os.killpg(child.pid,signal.SIGKILL);child.wait()
   raise
 assert code==0
log=(a.output/'run.log').read_text();demo=json.loads((a.output/'paint.json').read_text());process=SimpleNamespace(pid=demo['pid']);result={}
assert f'BTS_SHUTDOWN clean pid={process.pid}' in log
assert not re.search(r'BTS_HOST_FAILED|BTS_DRAW_FAILED|Shader compilation failed|BTS_FINALIZE_FALLBACK',log,re.I)
actual=re.findall(r'BTS_GPU_STACK_VERIFIED channel=(\d+) probes=64 max_error=(\S+) valid=(\d+)',log)
assert len(actual)>100 and all(row[2]=='1' for row in actual)
demo=json.loads((a.output/'paint.json').read_text());assert demo['pid']==process.pid
captures={c['stage']:c for c in demo['captures']};stages=['initial','setup','entered','assigned','projected3d','paint-restored','projection-restored','transformed','projected-uv','undo','redo','tiling','standard','aces','uniform','weave-menu','paint-return','saved','reopened','entered-open','assigned-open','paint-open','undo-open','paint-final']
assert list(captures)==stages
def data(stage):return captures[stage]['projection']
def target(stage):return next(t for t in captures[stage]['document']['texture_sets'] if t['name']=='A_ProjectionSurface')
def other(stage):return next(t for t in captures[stage]['document']['texture_sets'] if t['name']=='Z_OtherSurface')
defaults=data('entered');assert defaults['active'] and defaults['source']=='uniform color'
assert defaults['size']==10 and defaults['spacing']==20 and defaults['alpha']=='Shape' and defaults['pressure_size'] and defaults['minimum_size']==5
assert defaults['flow']==defaults['opacity']==100 and defaults['angle']==0 and not defaults['follow'] and defaults['channels']==[True]*5 and defaults['tiling']==0
for stage in ['assigned','projection-restored','weave-menu','assigned-open']:assert data(stage)['active'] and data(stage)['source']=='Blue Weave' and data(stage)['channels']==[True,True,True,False,True]
assert abs(data('assigned')['roughness']-.8824)<1e-6
for stage in ['paint-restored','paint-return']:assert not data(stage)['active'] and data(stage)['size']==15 and data(stage)['spacing']==100 and data(stage)['alpha']=='Sign Plus' and not data(stage)['pressure_size']
assert data('projected3d')['source_error']<2e-4 and data('projected3d')['native_error']<2e-6 and data('projected3d')['samples']>=5,data('projected3d')
assert data('transformed')['offset']!=[0,0] and data('transformed')['scale']>1 and abs(data('transformed')['rotation']-20)<1e-6
assert data('tiling')['tiling']==3 and data('tiling')['active'] and data('tiling')['source']=='Blue Weave' and data('tiling')['hardness']==1 and data('uniform')['source']=='uniform color'
control_pairs=[('setup','entered'),('entered','assigned'),('projected3d','paint-restored'),('paint-restored','projection-restored'),('projection-restored','transformed'),('redo','tiling'),('tiling','standard'),('standard','aces'),('aces','uniform'),('uniform','weave-menu'),('weave-menu','paint-return'),('reopened','entered-open'),('entered-open','assigned-open')]
for before,after in control_pairs:
    assert captures[before]['document']==captures[after]['document'],(before,after)
    for key in ['hash','native_hash','rough_hash']:assert data(before)[key]==data(after)[key],(before,after,key)
for before,after in [('assigned','projected3d'),('transformed','projected-uv'),('assigned-open','paint-open')]:assert data(before)['hash']!=data(after)['hash'] and data(before)['native_hash']!=data(after)['native_hash'],(before,after)
for before,after in [('projected3d','undo'),('projected-uv','redo'),('redo','saved'),('saved','reopened'),('reopened','undo-open')]:
    for key in ['hash','native_hash','rough_hash']:assert data(before)[key]==data(after)[key],(before,after,key)
for stage in stages[1:]:
    assert other(stage)==other('setup'),stage
    assert len(target(stage)['layers'])==len(target('setup')['layers']),stage
    assert data(stage)['old_sample']==data('setup')['old_sample'],stage
    for key in ['mesh_objects','uv_triangles','bounds_minimum','bounds_maximum']:assert captures[stage]['native'][key]==captures['setup']['native'][key],(stage,key)
    if stages.index(stage)<18:assert captures[stage]['view']['scene_view_matrix']==captures['setup']['view']['scene_view_matrix'],stage
result['material_projection_demo']={'pid':process.pid,'stages':stages,'independent_retained_paint_and_projection_profiles':True,'real_shelf_material_assignment_without_new_layers':True,'per_texel_source_color_and_roughness_max_error':data('projected3d')['source_error'],'native_composite_max_error':data('projected3d')['native_error'],'source_samples':data('projected3d')['samples'],'world_and_uv_screen_projection':True,'old_marks_and_other_texture_set_preserved':True,'s_transforms_tiling_and_brush_preset_keeps_projection_source':True,'ocio_overlay_without_transforming_authored_data':True,'undo_redo_packed_open_further_paint':True,'scope':'Original Blue Weave multichannel resource; no Fabric Denim/SBSAR runtime or physical particle-projection claim.'}
assert 'BTS_GPU_MATERIAL_PROJECTION_VERIFIED' in log
assert 'BTS_BRUSH_CPU_FALLBACK' not in log and 'BTS_GPU_AUTHORING_CPU_READ' not in log
result['gpu']={'checks':len(actual),'maximum_error':max(float(row[1]) for row in actual),'cpu_brush_fallbacks':0}
(a.output/'material-projection-gpu-verified.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
