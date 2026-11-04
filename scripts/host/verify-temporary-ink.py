"""Ctrl switches ink mid-stroke without mutating either selected color."""
from parity_test_environment import private_display
import argparse,json,os,subprocess
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);a=p.parse_args();a.output=a.output.resolve();a.output.mkdir(parents=True,exist_ok=True);root=Path(__file__).resolve().parents[2]
env=dict(os.environ,LANG='sv_SE.UTF-8',LC_ALL='sv_SE.UTF-8',QT_QPA_PLATFORM='offscreen',BTS_HEADLESS_WORKSPACE='1',DISPLAY=private_display(),XDG_RUNTIME_DIR='/run/user/1000',BTS_TEMPORARY_INK_DEMO='1',BTS_SKEW_DEMO='1',BTS_PERFORMANCE_PROFILE='1',BTS_TEMPLATE_DIR=str(a.output/'templates'))
with (a.output/'run.log').open('w')as log:subprocess.run(['bash',str(root/'scripts/host/run-host.sh'),'--workspace','--gui-fix-demo',str(a.output),'--gui-fix-mesh',str(root/'fixtures/skew_low.obj'),'--gui-fix-resolution','128','--autosave-seconds','3600','--library-dir',str(a.output/'library')],env=env,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=240)
d=json.loads((a.output/'temporary-ink.json').read_text());c={x['stage']:x for x in d['captures']}
for s in c.values():assert s['primary']==[1,0,0,1]and s['secondary']==[0,0,1,1]and not s['temporary_active']
def near(a,b):return max(abs(x-y)for x,y in zip(a,b))<.015
assert all(near(a,b)for a,b in zip(c['color']['samples'],[[1,0,0],[0,0,1],[1,0,0]]))
assert c['color']['undo_count']==c['initial']['undo_count']+1
assert c['redo']['samples']==c['color']['samples']and c['undo']['samples']==c['initial']['samples']
assert c['black-default']['mask_ink']==1 and c['white-default']['mask_ink']==0
assert near(c['black-mask']['masks'],[1,0,1])and near(c['white-mask-tablet']['masks'],[0,1,0])
for stage,mask in [('black-mask',[1,0,1]),('white-mask-tablet',[0,1,0])]:
 for i,value in enumerate(mask):assert near(c[stage]['samples'][i],c['color']['samples'][i]if value else c['initial']['samples'][i])
assert c['black-mask']['undo_count']==c['black-default']['undo_count']+1
assert c['white-mask-tablet']['first_dab_diameter']<c['white-mask-tablet']['last_dab_diameter']
assert c['white-mask-tablet']['undo_count']==c['white-default']['undo_count']+1
(a.output/'verified.json').write_text(json.dumps(d,indent=2)+'\n');print('Temporary color/mask mouse+tablet GPU pixels and one Undo PASS')
