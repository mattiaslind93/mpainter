# SPDX-License-Identifier: GPL-3.0-or-later
"""Real stencil gestures, GPU brush masking and packed project roundtrip."""
import argparse,json,os,re,subprocess,signal
from pathlib import Path
from types import SimpleNamespace
p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);p.add_argument('--environment',type=Path);p.add_argument('--existing',action='store_true');a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True)
root=Path(__file__).resolve().parents[2];build=root.parent/'build'
if not a.existing:
 assert a.environment, "--environment must name the NewProject fixture HDRI"
 fixture=a.output/'brush-stencil.obj'
 fixture.write_text('mtllib brush-stencil.mtl\no BrushPlane\nusemtl BrushSurface\nv -1 -1 0\nv 1 -1 0\nv 1 1 0\nv -1 1 0\nvt 0 0\nvt 1 0\nvt 1 1\nvt 0 1\nf 1/1 2/2 3/3 4/4\n')
 fixture.with_suffix('.mtl').write_text('newmtl BrushSurface\nKd .5 .5 .5\n')
 command=['bash',str(root/'scripts/host/run-host.sh'),'--workspace','--project',str(build/'baseline-demo/host-fixture.blend'),'--environment',str(a.environment),'--new-mesh',str(root/'native/tests/fixtures/single_tile.obj'),'--new-demo-dir',str(a.output),'--stencil-demo',str(fixture),'--recovery-dir',str(a.output/'recovery')]
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
captures={c['stage']:c for c in demo['captures']};stages=['initial','setup','empty-tiling','assigned','stencil3d','translated','rotated','scaled','cancelled-transform','reset','cleared','plain-stroke','undo-plain','shelf-drop','stencil2d','undo-uv','redo-uv','ignored-stroke','undo-ignored','bitmap-import','bitmap-stroke','undo-bitmap','saved','reopened','paint-open','undo-open']
assert list(captures)==stages
def data(stage):return captures[stage]['stencil']
assert data('setup')['choices']==['No Tiling','Horizontal Tiling','Vertical Tiling','H and V Tiling']
assert data('setup')['resource']=='' and data('setup')['tiling']==0
assert data('empty-tiling')['tiling']==3 and data('empty-tiling')['resource']==''
assert data('assigned')['resource']=='Sign Plus' and data('assigned')['scale']==.25
assert data('stencil3d')['reference_error']<2e-6 and data('stencil3d')['white']>0 and data('stencil3d')['black']>0,data('stencil3d')
assert data('translated')['offset']!=[0,0]
assert abs(data('rotated')['angle']-35)<1e-6
assert data('scaled')['scale']>data('rotated')['scale']
for field in ['scale','angle','offset']:assert data('cancelled-transform')[field]==data('scaled')[field]
assert data('reset')['scale']==1 and data('reset')['angle']==0 and data('reset')['offset']==[0,0]
assert data('cleared')['resource']=='' and data('shelf-drop')['resource']=='Sign Plus'
assert data('bitmap-import')['resource']=='half-stencil'
for before,after in [('setup','empty-tiling'),('empty-tiling','assigned'),('stencil3d','translated'),('translated','rotated'),('rotated','scaled'),('scaled','cancelled-transform'),('cancelled-transform','reset'),('reset','cleared'),('undo-plain','shelf-drop'),('undo-ignored','bitmap-import')]:
    assert captures[before]['document']==captures[after]['document'],(before,after)
    assert data(before)['hash']==data(after)['hash'] and data(before)['native_hash']==data(after)['native_hash'],(before,after)
for before,after in [('assigned','stencil3d'),('cleared','plain-stroke'),('shelf-drop','stencil2d'),('redo-uv','ignored-stroke'),('bitmap-import','bitmap-stroke'),('reopened','paint-open')]:
    assert data(before)['hash']!=data(after)['hash'] and data(before)['native_hash']!=data(after)['native_hash'],(before,after)
for before,after in [('stencil3d','undo-plain'),('stencil3d','undo-uv'),('stencil2d','redo-uv'),('stencil2d','undo-ignored'),('stencil2d','undo-bitmap'),('stencil2d','saved'),('saved','reopened'),('reopened','undo-open')]:
    assert data(before)['hash']==data(after)['hash'] and data(before)['native_hash']==data(after)['native_hash'],(before,after)
for stage in stages[1:]:
    assert not data(stage)['ignore']
    for key in ['mesh_objects','uv_triangles','bounds_minimum','bounds_maximum']:assert captures[stage]['native'][key]==captures['setup']['native'][key],(stage,key)
    if stages.index(stage)<23:assert captures[stage]['view']['scene_view_matrix']==captures['setup']['view']['scene_view_matrix'],stage
result['stencil_demo']={'pid':process.pid,'stages':stages,'per_texel_3d_camera_mask_max_error':data('stencil3d')['reference_error'],'real_resource_and_shelf_drop':True,'image_import_and_non_square_resource':True,'four_tiling_choices_and_empty_noop':True,'s_translate_rotate_scale_escape_and_reset_preserve_paint_and_camera':True,'uv_masking_and_temporary_n_bypass':True,'undo_redo_and_packed_open':True,'scope':'Own viewport-space scalar stencil; legacy empty state plus current Adobe gesture contract. Transform defaults and overlay appearance are not claimed as measured Painter parity.'}
assert 'BTS_GPU_STENCIL_VERIFIED' in log
assert 'BTS_BRUSH_CPU_FALLBACK' not in log and 'BTS_GPU_AUTHORING_CPU_READ' not in log
result['gpu']={'checks':len(actual),'maximum_error':max(float(row[1]) for row in actual),'cpu_brush_fallbacks':0}
(a.output/'stencil-gpu-verified.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
