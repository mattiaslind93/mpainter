"""Property event-to-visible latency and per-stage native composite timings."""
from parity_test_environment import private_display
import argparse,json,os,re,subprocess,statistics
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);p.add_argument('--baseline',action='store_true');a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True);root=Path(__file__).resolve().parents[2]
env=dict(os.environ,LANG='sv_SE.UTF-8',LC_ALL='sv_SE.UTF-8',DISPLAY=private_display(),XDG_RUNTIME_DIR='/run/user/1000',BTS_PROPERTY_PERFORMANCE='1',BTS_FILL_TEXTURE_DEMO='1',BTS_FILL_CHANNELS_DEMO='1')
command=['bash',str(root/'scripts/host/run-host.sh'),'--workspace','--gui-fix-demo',str(a.output),'--gui-fix-mesh',str(root/'fixtures/single_tile.obj'),'--gui-fix-resolution','1024','--recovery-dir',str(a.output/'recovery')]
with (a.output/'run.log').open('w') as log:
 process=subprocess.Popen(command,env=env,stdout=log,stderr=subprocess.STDOUT)
 try:code=process.wait(timeout=900)
 except subprocess.TimeoutExpired:process.terminate();process.wait(timeout=15);raise
assert code==0,code
proof=json.loads((a.output/'property-performance.json').read_text());assert proof['pid']==process.pid
rows=[]
for case in proof['cases']:
 visible=case['during_drag']['visible'];latency=[v['visible_ms'] for v in visible]
 row=dict(property=case['property'],max_ping_ms=case['max_ping_ms'],visible_during_drag=len(visible),median_visible_ms=statistics.median(latency) if latency else None,max_visible_ms=max(latency) if latency else None,undo_steps=case['undo_after']-case['undo_before'],settled_ms=case['after_release_ms'],upload_counts=case['upload_counts']);rows.append(row)
 if not a.baseline:
  assert row['max_ping_ms']<100,row
  assert row['visible_during_drag']>=3 and row['median_visible_ms']<300,row
  assert row['undo_steps']==1,row
  assert all(count==0 for count in row['upload_counts'][1:]),row
  assert case['before_hash']!=case['after_hash'],row
if not a.baseline:
 for case in proof['atomic']:
  assert case['max_ping_ms']<300,dict(property=case['property'],ping=case['max_ping_ms'])
  assert case['undo_steps']==1,case['property']
  assert case['visible'] and min(v['visible_ms'] for v in case['visible'])<300,case['property']
  assert not case['reference']['composite_differences'],case['property']
  if case['property'] not in ('projection','projection_transform'):
   changed=2 if case['property']=='channel_texture' else 0
   assert all(count==0 for c,count in enumerate(case['upload_counts']) if c!=changed),(case['property'],case['upload_counts'])
 assert proof['undo']['fill_channels']!=proof['reference']['fill_channels']
 assert proof['undo']['redo']['fill_channels']==proof['reference']['fill_channels']
 assert proof['reference']['document']==proof['final']['document']
 assert proof['reference']['fill_channels']==proof['final']['fill_channels']
 assert not (a.output/'performance-checker.png').exists()
final=proof['final'];assert final['composite_checked'] and not final['composite_differences'],final.get('composite_differences');assert final['decimal']=='.' and not final['shader_failures']
log=(a.output/'run.log').read_text();assert f'BTS_SHUTDOWN clean pid={process.pid}' in log;assert not re.search(r'BTS_HOST_FAILED|BTS_DRAW_FAILED|Shader compilation failed|BTS_FINALIZE_FALLBACK',log,re.I),log[-3000:]
if env.get('BTS_HEADLESS_WORKSPACE')=='1' and not a.baseline:
 assert log.count('BTS_HEADLESS_PAINT_DEVICE_CREATED')==1, 'Qt paint device recreated across frames'
 assert 'BTS_GL_ERROR' not in log, 'GL error while reusing Qt paint device'
result=dict(pid=process.pid,baseline=a.baseline,locale='sv_SE.UTF-8',cases=rows,exact_final_composite=True);(a.output/'verified.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
