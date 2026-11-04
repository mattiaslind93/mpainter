"""Native panel wheel routing and middle-button panning through real widget events."""
from parity_test_environment import private_display
import argparse,json,os,re,subprocess
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True);root=Path(__file__).resolve().parents[2]
env=dict(os.environ,LANG='sv_SE.UTF-8',LC_ALL='sv_SE.UTF-8',DISPLAY=private_display(),XDG_RUNTIME_DIR='/run/user/1000',BTS_SCROLL_DEMO='1')
command=['bash',str(root/'scripts/host/run-host.sh'),'--workspace','--gui-fix-demo',str(a.output),'--gui-fix-mesh',str(root/'fixtures/single_tile.obj'),'--gui-fix-resolution','512','--recovery-dir',str(a.output/'recovery')]
with (a.output/'run.log').open('w') as log:
 process=subprocess.Popen(command,env=env,stdout=log,stderr=subprocess.STDOUT)
 try:code=process.wait(timeout=120)
 except subprocess.TimeoutExpired:process.terminate();process.wait(timeout=15);raise
assert code==0,code
proof=json.loads((a.output/'panel-scroll.json').read_text());assert proof['pid']==process.pid
checks={v['test']:v for v in proof['checks']}
required=['roughness-wheel-scrolls-properties','roughness-value-preserved','middle-pan-properties','toolbar-value-preserved','scroll-has-no-undo','scale-wheel-scrolls-settings','scale-value-preserved','middle-pan-settings','settings-scroll-has-no-undo','middle-pan-layers','layer-selection-preserved']
assert all(checks[k]['passed'] for k in required)
assert all(v['passed'] for v in checks.values())
assert proof['state']['decimal']=='.' and not proof['state']['shader_failures']
log=(a.output/'run.log').read_text();assert not re.search(r'Shader compilation failed|error C\d+|BTS_DRAW_FAILED|BTS_HOST_FAILED|BTS_FINALIZE_FALLBACK',log,re.I),log[-4000:]
assert f'BTS_SHUTDOWN clean pid={process.pid}' in log
result=dict(pid=process.pid,locale='sv_SE.UTF-8',checks=proof['checks'],no_numeric_wheel_edits=True,properties_and_settings_scroll=True,middle_pan=True,selection_and_undo_preserved=True)
(a.output/'verified.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
