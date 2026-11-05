"""Real Quick Mask gestures, GPU overlay and packed project isolation."""
import argparse,json,os,re,subprocess,signal
from pathlib import Path
from types import SimpleNamespace
p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);p.add_argument('--environment',type=Path);p.add_argument('--existing',action='store_true');a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True)
root=Path(__file__).resolve().parents[2];build=root.parent/'build'
if not a.existing:
 assert a.environment, "--environment must name the NewProject fixture HDRI"
 command=['bash',str(root/'scripts/host/run-host.sh'),'--workspace','--project',str(build/'baseline-demo/host-fixture.blend'),'--environment',str(a.environment),'--new-mesh',str(root/'native/tests/fixtures/single_tile.obj'),'--new-demo-dir',str(a.output),'--quick-mask-demo',str(build/'quick-mask-native/material-projection.obj'),'--recovery-dir',str(a.output/'recovery')]
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
assert 'BTS_GPU_QUICK_MASK_VERIFIED' in log and 'BTS_QUICK_MASK_GPU_RECORD' in log
assert 'BTS_GPU_QUICK_GATE_VERIFIED' in log and 'BTS_BRUSH_CPU_FALLBACK' not in log
assert 'BTS_GPU_AUTHORING_CPU_READ' not in log
assert 'BTS_GPU_CPU_FALLBACK' not in log and 'BTS_GPU_IMAGE_BOOTSTRAP' in log
demo=json.loads((a.output/'paint.json').read_text());assert demo['pid']==process.pid
captures={c['stage']:c for c in demo['captures']};stages=['initial', 'setup', 'enabled', 'mask-painted', 'edit-off', 'restricted', 'cleared', 'undo', 'redo', 'enabled2', 'inverted', 'edit-off2', 'blocked', 'cleared2', 'filled-gap', 'undo-fill', 'saved-plain', 'enabled3', 'masked3', 'saved-masked', 'reopened', 'paint-open', 'undo-open']
assert list(captures)==stages
def data(stage):return captures[stage]['quick_mask']
def target(stage):return next(t for t in captures[stage]['document']['texture_sets'] if t['name']=='A_ProjectionSurface')
def other(stage):return next(t for t in captures[stage]['document']['texture_sets'] if t['name']=='Z_OtherSurface')
d=data('enabled');assert d['enabled'] and d['editing'] and d['size']==10 and d['flow']==100 and d['spacing']==20 and d['alpha']=='Shape' and d['gray']==0 and d['gray_visible'] and 'GRAYSCALE' in d['title']
assert data('mask-painted')['center_allowed']<.01
assert data('edit-off')['enabled'] and not data('edit-off')['editing'] and data('edit-off')['size']==15 and data('edit-off')['alpha']=='Sign Plus' and not data('edit-off')['gray_visible']
assert data('restricted')['gap'][3]<.05*min(data('restricted')['left'][3],data('restricted')['right'][3]) and data('restricted')['left'][3]>.1 and data('restricted')['right'][3]>.1,data('restricted')
assert data('restricted')['undo_count']==data('edit-off')['undo_count']+1
assert data('inverted')['center_allowed']==0 and data('blocked')['center_allowed']==0
assert data('filled-gap')['gap'][3]>.1,data('filled-gap')
for before,after in [('setup','enabled'),('setup','mask-painted'),('setup','edit-off'),('setup','undo'),('restricted','cleared'),('restricted','redo'),('redo','enabled2'),('redo','inverted'),('redo','blocked'),('redo','cleared2'),('redo','undo-fill'),('saved-plain','enabled3'),('saved-plain','masked3'),('saved-plain','saved-masked'),('saved-masked','reopened'),('reopened','undo-open')]:
    assert captures[before]['document']==captures[after]['document'],(before,after)
    assert data(before)['hashes']==data(after)['hashes'],(before,after)
assert data('blocked')['undo_count']==data('edit-off2')['undo_count']
assert data('reopened')['native_hash']==data('saved-plain')['native_hash'] and not data('reopened')['enabled'] and not data('reopened')['editing'] and data('reopened')['mask_count']==0
assert data('paint-open')['hashes'][0]!=data('reopened')['hashes'][0]
assert data('cleared')['native_hash']!=data('restricted')['native_hash'] and data('cleared')['gap']==data('restricted')['gap']
for stage in stages[1:]:
    assert other(stage)==other('setup') and len(target(stage)['layers'])==len(target('setup')['layers']),stage
result['quick_mask_demo']={'pid':process.pid,'stages':stages,'temporary_mask_edits_leave_document_and_layers_unchanged':True,'grayscale_brush_profile_and_ordinary_plus_restored':True,'world_mask_restricts_subsequent_world_stroke_with_near_empty_gap':True,'y_clears_preview_and_preserves_rejected_coverage':True,'i_inverts_selected_mask_and_blocks_entire_surface':True,'clear_allows_gap_fill_and_one_gesture_undo_restores_gap':True,'native_save_open_excludes_temporary_overlay_and_further_paint_undo_pass':True,'other_texture_set_data_unchanged':True}
result['gpu']={'checks':len(actual),'maximum_error':max(float(row[1]) for row in actual),'quick_mask_batches':log.count('BTS_QUICK_MASK_GPU_RECORD'),'gated_strokes':'GPU coverage sampling, no CPU brush fallback'}
(a.output/'quick-mask-gpu-verified.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
