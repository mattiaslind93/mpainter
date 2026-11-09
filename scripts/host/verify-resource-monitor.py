# SPDX-License-Identifier: GPL-3.0-or-later
"""Real image overwrite -> separate Shelf/project refresh -> immutable Undo and packed reopen."""
from parity_test_environment import private_display
import argparse,json,os,re,subprocess
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);p.add_argument('--profile',action='store_true');a=p.parse_args();a.output=a.output.resolve();a.output.mkdir(parents=True,exist_ok=True);root=Path(__file__).resolve().parents[2]
env=dict(os.environ,LANG='sv_SE.UTF-8',LC_ALL='sv_SE.UTF-8',DISPLAY=private_display(),XDG_RUNTIME_DIR='/run/user/1000',XDG_CONFIG_HOME=str(a.output/'config'),BTS_RESOURCE_MONITOR_DEMO='1',BTS_FILL_TEXTURE_DEMO='1',BTS_FILL_CHANNELS_DEMO='1')
if a.profile:env['BTS_PERFORMANCE_PROFILE']='1'
cmd=['bash',str(root/'scripts/host/run-host.sh'),'--workspace','--gui-fix-demo',str(a.output),'--gui-fix-mesh',str(root/'native/tests/fixtures/single_tile.obj'),'--gui-fix-resolution','512','--library-dir',str(a.output/'shelf'),'--recovery-dir',str(a.output/'recovery')]
with (a.output/'run.log').open('w') as log:
 proc=subprocess.Popen(cmd,env=env,stdout=log,stderr=subprocess.STDOUT)
 try:code=proc.wait(timeout=300)
 except subprocess.TimeoutExpired:proc.terminate();proc.wait(timeout=15);raise
assert code==0,(code,(a.output/'run.log').read_text()[-3000:])
d=json.loads((a.output/'resource-monitor.json').read_text());assert d['pid']==proc.pid;c={s['stage']:s for s in d['captures']}
def channel(stage,n):return c[stage]['fill_channels'][n]
def catalog(stage,scope):return [x for x in c[stage]['resource_catalog'] if x['scope']==scope]
def bound(stage):
 doc=c[stage]['document'];layer=doc['texture_sets'][0]['layers'][-1]
 return layer['fill_projection']
assert not c['defaults']['library_auto'] and not c['defaults']['project_auto'] and c['defaults']['skip_mismatch']
assert catalog('imported','user')[0]['sha256']!=catalog('library-only','user')[0]['sha256']
for n in [0,2]:
 assert channel('assigned',n)==channel('library-only',n)
 assert channel('project-updated',n)['hash']!=channel('assigned',n)['hash']
 assert channel('undo',n)==channel('assigned',n)
 assert channel('redo',n)==channel('project-updated',n)
 assert channel('saved-undo',n)==channel('opened-undo',n)==channel('assigned',n)
 assert channel('automatic',n)['hash']!=channel('opened-undo',n)['hash']
 assert channel('missing',n)==channel('automatic',n)
 assert channel('recovered-resized',n)['hash']!=channel('missing',n)['hash']
 assert channel('saved',n)==channel('opened-packed',n)
assert abs(channel('assigned',2)['source_first'][0]-64/255)<1e-6
assert abs(channel('project-updated',2)['source_first'][0]-192/255)<1e-6
assert abs(channel('project-updated',0)['source_first'][0]-192/255)>.01
assert c['project-updated']['undo_count']==c['library-only']['undo_count']+1
assert c['project-updated']['monitor_completed']-c['library-only']['monitor_completed']<=2
assert c['automatic']['project_auto'] and not c['automatic']['library_auto']
assert c['automatic']['undo_count']==c['opened-undo']['undo_count']+1
assert c['missing']['monitor_messages'] and not c['recovered-resized']['monitor_messages']
assert bound('recovered-resized')['texture']['width']==64
assert c['reference']['composite_checked'] and not c['reference']['composite_differences']
assert c['saved-undo']['document']==c['opened-undo']['document']
assert c['saved']['document']==c['opened-packed']['document']
assert not (a.output/'monitored.png').exists()
assert len(bound('sequence-one')['texture']['tiles'])==1
assert bound('sequence-skipped')['texture']==bound('sequence-one')['texture']
assert any('mismatch' in m for m in c['sequence-skipped']['monitor_messages'])
assert len(bound('sequence-accepted')['texture']['tiles'])==2
assert c['sequence-accepted']['undo_count']==c['sequence-skipped']['undo_count']+1
assert c['sequence-saved']['document']==c['sequence-opened']['document']
assert channel('sequence-saved',0)==channel('sequence-opened',0)
assert d['max_ping_ms']<100,d['max_ping_ms']
for s in c.values():assert s['decimal']=='.' and not s['shader_failures']
log=(a.output/'run.log').read_text();assert f'BTS_SHUTDOWN clean pid={proc.pid}' in log;assert not re.search(r'BTS_HOST_FAILED|BTS_DRAW_FAILED|Shader compilation failed',log,re.I)
result=dict(pid=proc.pid,locale='sv_SE.UTF-8',off_by_default=True,separate_library_project_updates=True,atomic_image_replacement=True,coalesced_requests=True,raw_and_acescg_updated=True,one_undo_for_shared_resource=True,save_after_undo_exact=True,automatic_interval_refresh=True,missing_source_retains_version=True,resized_source_recovers=True,exact_final_composite=True,packed_open_without_original=True,udim_mismatch_skip_and_force=True,udim_packed_open_exact=True,max_ping_ms=d['max_ping_ms'])
(a.output/'verified.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
