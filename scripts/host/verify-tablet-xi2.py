"""Run on graphical Linux HOST, evdev installed: kernel→Wayland→XWayland→Qt→paint.
Adds a virtual tablet after startup, removes it, then creates another tablet.
Physical Wacom/replug/suspend remain a separate Mattias acceptance test.
"""
from parity_test_environment import private_display
from session_guard import require_gui_allowed
require_gui_allowed()
import argparse,json,os,subprocess,time
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True);root=Path(__file__).resolve().parents[2]
env=dict(os.environ,LANG='sv_SE.UTF-8',LC_ALL='sv_SE.UTF-8',DISPLAY=private_display(),XDG_RUNTIME_DIR='/run/user/1000',BTS_TABLET_REGRESSION='1',BTS_TABLET_XI2='1',BTS_TABLET_LOG='1',QT_FORCE_STDERR_LOGGING='1',QT_LOGGING_RULES='qt.qpa.input*=true')
# The container runs the app; uinput must be created on the compositor's host.
cmd=['distrobox','enter','bts-build','--','env','XDG_CONFIG_HOME='+str(a.output/'config'),'XDG_DATA_HOME='+str(a.output/'data'),'bash',str(root/'scripts/host/run-host.sh'),'--workspace','--gui-fix-demo',str(a.output),'--gui-fix-mesh',str(root/'native/tests/fixtures/single_tile.obj'),'--gui-fix-resolution','512','--recovery-dir',str(a.output/'recovery')]
with (a.output/'run.log').open('w') as log:
 proc=subprocess.Popen(cmd,env=env,stdout=log,stderr=subprocess.STDOUT)
 try:
  for run in range(2):
   ready=a.output/f'xi2-ready-{run}.json';deadline=time.monotonic()+90
   while not ready.exists():
    assert proc.poll() is None,(proc.returncode,(a.output/'run.log').read_text()[-2500:])
    assert time.monotonic()<deadline,'XI2 ready timeout'
    time.sleep(.1)
   subprocess.run(['python3',str(root/'scripts/host/inject-tablet-uinput.py'),str(ready)],check=True,timeout=15)
  code=proc.wait(timeout=60)
 finally:
  if proc.poll() is None:proc.terminate();proc.wait(timeout=15)
assert code==0,code
proof=json.loads((a.output/'tablet-pressure-xi2.json').read_text());assert proof['kernel_virtual_device'] and not proof['physical_tablet_verified'] and len(proof['runs'])==2
rows=[]
for run in proof['runs']:
 dabs=run['spline_dabs'];assert len(dabs)>20,len(dabs)
 assert run['hash']!=run['before']['hash'] and run['undo_count']==run['before']['undo_count']+1
 assert run['composite_checked'] and not run['composite_differences'] and not run['shader_failures']
 for key in ('pressure','diameter','flow'):
  values=[d[key] for d in dabs];assert max(values)>min(values)*3,(key,values);assert all(y>=x-2e-4 for x,y in zip(values,values[1:])),(key,values)
 rows.append(dict(device=run['pointer_device'],dabs=len(dabs),min_pressure=min(d['pressure'] for d in dabs),max_pressure=max(d['pressure'] for d in dabs),hash=run['hash'],undo_count=run['undo_count']))
log=(a.output/'run.log').read_text();assert 'XI2 event on tablet' in log and '"event":"dab"' in log and '"xwayland_root_coordinates":true' in log
result=dict(pid=proof['pid'],kernel_virtual_device=True,backend='xcb/XWayland',late_device_and_recreation=True,runs=rows,physical_wacom_verified=False)
(a.output/'verified.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
