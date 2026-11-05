"""Real Qt drag events: nesting, root extraction, collapsed and multi-selection."""
from parity_test_environment import private_display
import argparse,json,os,subprocess
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);a=p.parse_args();a.output=a.output.resolve();a.output.mkdir(parents=True,exist_ok=True);root=Path(__file__).resolve().parents[2]
env=dict(os.environ,LANG='sv_SE.UTF-8',LC_ALL='sv_SE.UTF-8',QT_QPA_PLATFORM='offscreen',BTS_HEADLESS_WORKSPACE='1',DISPLAY=private_display(),XDG_RUNTIME_DIR='/run/user/1000',BTS_LAYER_DROP_DEMO='1',BTS_SKEW_DEMO='1',BTS_PERFORMANCE_PROFILE='1',BTS_TEMPLATE_DIR=str(a.output/'templates'))
with (a.output/'run.log').open('w')as log:subprocess.run(['bash',str(root/'scripts/host/run-host.sh'),'--workspace','--gui-fix-demo',str(a.output),'--gui-fix-mesh',str(root/'native/tests/fixtures/single_tile.obj'),'--gui-fix-resolution','128','--autosave-seconds','3600','--library-dir',str(a.output/'library')],env=env,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=240)
d=json.loads((a.output/'layer-drop.json').read_text());c={x['stage']:x for x in d['captures']}
def layers(stage):return c[stage]['document']['texture_sets'][0]['layers']
def item(stage,key):return next(x for x in layers(stage)if x['id']==d[key])
def parent(stage,key):return item(stage,key).get('parent','')
assert parent('inside-a','paint')==d['a'];assert parent('inside-b','paint')==d['b']
assert parent('collapsed-received','second')==d['b'];assert not parent('root','paint')
assert parent('multiple-in-a','paint')==parent('multiple-in-a','second')==d['a']
assert parent('nested','a')==d['b'];assert parent('nested','paint')==d['a']
assert layers('undo')==layers('multiple-in-a');assert layers('redo')==layers('nested')==layers('reopened')
(a.output/'verified.json').write_text(json.dumps(d,indent=2)+'\n');print('Native hierarchy drop PASS')
