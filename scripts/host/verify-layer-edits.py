"""Real 2048px GUI layer edits: responsive preview, exact finalize and persistence."""
from parity_test_environment import private_display
import argparse,json,os,re,subprocess
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True)
root=Path(__file__).resolve().parents[2]
env=dict(os.environ,LANG='sv_SE.UTF-8',LC_ALL='sv_SE.UTF-8',DISPLAY=private_display(),XDG_RUNTIME_DIR='/run/user/1000',BTS_LAYER_EDIT_DEMO='1')
command=['bash',str(root/'scripts/host/run-host.sh'),'--workspace','--gui-fix-demo',str(a.output),'--gui-fix-mesh',str(root/'fixtures/single_tile.obj'),'--gui-fix-resolution','2048','--recovery-dir',str(a.output/'recovery')]
with (a.output/'run.log').open('w') as log:
 process=subprocess.Popen(command,env=env,stdout=log,stderr=subprocess.STDOUT)
 try:code=process.wait(timeout=480)
 except subprocess.TimeoutExpired:process.terminate();process.wait(timeout=15);raise
assert code==0,code
proof=json.loads((a.output/'layer-edit.json').read_text());assert proof['pid']==process.pid
captures={v['stage']:v for v in proof['captures']};ops=proof['operations'];assert len(ops)==23
assert sum(v['pages']>0 for v in captures['painted-three']['layer_items'])==3
for op in ops:
 assert op['return_ms']<100 and op['max_ping_ms']<100,op
 assert 0<=op['visible_ms']<150,op
 assert op['after']['undo_count']==op['before']['undo_count']+1,op
 assert op['after']['hash_bytes']==op['before']['hash_bytes'],op
assert not ops[0]['after']['composite_pending'] # Empty paint has no composite job.
assert captures['reference']['composite_checked'] and not captures['reference']['composite_differences']
assert captures['final']['layer_items']==captures['redo']['layer_items']==captures['opened']['layer_items']
assert captures['undo']['layer_items']!=captures['final']['layer_items']
assert captures['saved']['probe']==captures['opened']['probe']
assert all(c['decimal']=='.' and not c['shader_failures'] for c in captures.values())
log=(a.output/'run.log').read_text();assert 'BTS_LAYER_PREVIEW_MS' in log and 'BTS_EDIT_MS' in log
assert not re.search(r'Shader compilation failed|error C\d+|BTS_DRAW_FAILED|BTS_HOST_FAILED|BTS_LAYER_PREVIEW_FAILED|BTS_FINALIZE_FALLBACK',log,re.I),log[-4000:]
assert f'BTS_SHUTDOWN clean pid={process.pid}' in log
result=dict(pid=process.pid,resolution=2048,locale='sv_SE.UTF-8',operations=len(ops),max_ping_ms=max(o['max_ping_ms'] for o in ops),max_edit_ms=max(o['return_ms'] for o in ops),max_visible_ms=max(o['visible_ms'] for o in ops),exact_finalize=True,empty_paint_skips_composite=True,no_pixel_hashing_during_edits=True,undo_redo=True,save_open=True)
(a.output/'verified.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
