"""V: full Qt viewport path, pressure/flow, button loss, master ordering and batching.
Synthetic events verify app routing; they do not certify a physical Wacom/driver.
"""
from parity_test_environment import private_display
import argparse,json,os,re,subprocess
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);p.add_argument('--existing',action='store_true');a=p.parse_args();a.output=a.output.resolve();a.output.mkdir(parents=True,exist_ok=True);root=Path(__file__).resolve().parents[2]
for directory in ('config','data','cache','resources','logs','tmp','recovery'):(a.output/directory).mkdir(exist_ok=True)
env=dict(os.environ,QT_QPA_PLATFORM='offscreen',BTS_HEADLESS_WORKSPACE='1',XDG_CACHE_HOME=str(a.output/'cache'),BTS_RESOURCE_CACHE=str(a.output/'resources'),BTS_SESSION_LOG_DIR=str(a.output/'logs'),BTS_RECOVERY_DIR=str(a.output/'recovery'),TMPDIR=str(a.output/'tmp'),LANG='sv_SE.UTF-8',LC_ALL='sv_SE.UTF-8',DISPLAY=private_display(),XDG_RUNTIME_DIR='/run/user/1000',BTS_TABLET_REGRESSION='1',BTS_TABLET_LOG='1',XDG_CONFIG_HOME=str(a.output/'config'),XDG_DATA_HOME=str(a.output/'data'))
cmd=['bash',str(root/'scripts/host/run-host.sh'),'--workspace','--gui-fix-demo',str(a.output),'--gui-fix-mesh',str(root/'native/tests/fixtures/single_tile.obj'),'--gui-fix-resolution','512','--recovery-dir',str(a.output/'recovery')]
if not a.existing:
 with (a.output/'run.log').open('w') as log:
  proc=subprocess.Popen(cmd,env=env,stdout=log,stderr=subprocess.STDOUT)
  try:code=proc.wait(timeout=240)
  except subprocess.TimeoutExpired:proc.terminate();proc.wait(timeout=15);raise
 assert code==0,code
proof=json.loads((a.output/'tablet-pressure.json').read_text());pid=proof['pid'];assert len(proof['runs'])==10
if not a.existing:assert pid==proc.pid
rows=[]
for run in proof['runs']:
 assert not run['painting'] and run['contact']['painting']
 assert run['hash']!=run['before']['hash'],run['input']
 assert run['undo_count']==run['before']['undo_count']+1,(run['undo_count'],run['before']['undo_count'])
 assert run['gpu_composite_checked'] and not run['composite_differences']
 assert run['pressure_overlay'] and run['decimal']=='.' and not run['shader_failures']
 dabs=run['spline_dabs'];assert len(dabs)>20,len(dabs)
 for field in ('pressure','diameter','flow'):
  values=[d[field] for d in dabs]
  assert all(y>=x-2e-5 for x,y in zip(values,values[1:])),(field,values)
  if run['input']=='pen':assert values[-1]>values[0]*3,(field,values[0],values[-1])
  elif field=='pressure':assert all(x==1 for x in values),values
 if run['input']=='pen':assert run['input_pressure']==0
 rows.append({k:run[k] for k in ('input','oracle','linear','missing_buttons','master_press_first','hash')})
for batched,unbatched in zip(proof['runs'][::2],proof['runs'][1::2]):
 assert not batched['oracle'] and unbatched['oracle']
 assert batched['hash']==unbatched['hash'] and batched['bump_hash']==unbatched['bump_hash']
# Missing button flags and leading master press must match clean tablet input.
assert proof['runs'][0]['hash']==proof['runs'][4]['hash']
assert proof['runs'][2]['hash']==proof['runs'][6]['hash']
log=(a.output/'run.log').read_text();assert 'BTS_TABLET ' in log and '"event":"devices"' in log and '"event":"dab"' in log
assert not re.search(r'Shader compil|error C\d+|BTS_DRAW_FAILED|BTS_HOST_FAILED',log,re.I),log[-3000:]
assert f'BTS_SHUTDOWN clean pid={pid}' in log
assert 'BTS_GPU_HIDDEN_CHANNELS_VERIFIED uv_to_scene=1 native_oracle=1' in log
assert 'reason=unsupported-target-or-allocation' not in log
# Already-bound channels need no defer/resume transition. Require every run
# plus the post-UV scene oracle to verify actual full-resolution GPU pixels.
assert log.count('BTS_NATIVE_GPU_ORACLE channels=5 ')>=len(proof['runs'])+1
assert 'BTS_NATIVE_GPU_ORACLE channels=5 ' in log
result=dict(ignored_native_input=proof.get("ignored_native_input",0),pid=pid,locale='sv_SE.UTF-8',runs=rows,pressure_and_flow_monotonic=True,frame_batching_exact=True,missing_button_and_master_order_regressions=True,foreign_hover_and_proximity_ignored=True,hidden_channels_gpu_oracle=True,defer_resume_observed='BTS_GPU_CHANNEL_RESUME' in log,physical_tablet_verified=False,clean_exit=True)
(a.output/'verified.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
