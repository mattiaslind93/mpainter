"""Native double-click naming, cancellation, document history and packed roundtrip."""
import argparse,json,os,subprocess
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);a=p.parse_args();a.output=a.output.resolve();a.output.mkdir(parents=True,exist_ok=True);root=Path(__file__).resolve().parents[2]
env=dict(os.environ,BTS_SESSION_LOG_DIR=str(a.output/'logs'),BTS_RESOURCE_CACHE=str(a.output/'resource-cache'),LANG='sv_SE.UTF-8',LC_ALL='sv_SE.UTF-8',QT_QPA_PLATFORM='offscreen',BTS_HEADLESS_WORKSPACE='1',BTS_RENAME_DEMO='1',BTS_SKEW_DEMO='1',BTS_PERFORMANCE_PROFILE='1',BTS_TEMPLATE_DIR=str(a.output/'templates'),XDG_CONFIG_HOME=str(a.output/'config'),BTS_RECOVERY_DIR=str(a.output/'recovery'),XDG_DATA_HOME=str(a.output/'data'),XDG_CACHE_HOME=str(a.output/'cache'))
with (a.output/'run.log').open('w')as log:subprocess.run(['bash',str(root/'scripts/host/run-host.sh'),'--no-splash','--workspace','--gui-fix-demo',str(a.output),'--gui-fix-mesh',str(root/'fixtures/single_tile.obj'),'--gui-fix-resolution','128','--autosave-seconds','3600','--library-dir',str(a.output/'library')],env=env,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=240)
d=json.loads((a.output/'rename.json').read_text());c={x['stage']:x for x in d['captures']}
def model(stage):return c[stage]['document']['texture_sets'][0]
def names(stage):return [x['name']for x in model(stage)['layers']]
assert 'Ink – åäö'in names('layer-renamed')
assert names('layer-renamed')==names('cancelled')==names('empty-rejected')
assert 'Surface group'in names('folder-renamed')
for stage, expected in (('material-renamed','Hero Surface'),('material-f2','Keyboard Surface')):
 assert model(stage)['name']==expected and expected in c[stage]['native_material_names']
 assert c[stage]['texture_set_names'][0]==expected, (stage,c[stage]['texture_set_names'])
for stage in ('material-focus-out','material-cancelled','material-empty-rejected'):
 assert model(stage)['name']=='Hero Surface' and c[stage]['texture_set_names'][0]=='Hero Surface'
assert model('undo-material')['name']==model('initial')['name']
assert model('redo-material')['name']=='Hero Surface'
for stage in ('undo-material','redo-material'):
 assert c[stage]['texture_set_names'][0]==model(stage)['name']
assert names('reopened')==names('redo-material') and model('reopened')['name']=='Hero Surface'
assert 'Hero Surface'in c['reopened']['native_material_names']
assert c['shader-renamed']['shader_name']==c['redo-shader']['shader_name']==c['reopened']['shader_name']=='Hero Shader'
assert c['undo-shader']['shader_name']==c['initial']['shader_name']
for stage in ('shader-renamed','redo-shader','reopened','reopened-again'):
 assert c[stage]['document']['shader_instances']['instances'][0]['name']=='Hero Shader'
 assert c[stage]['texture_set_shader_names'][0]=='Hero Shader'
for stage in ('reopened-renamed','reopened-again'):
 assert model(stage)['name']=='Reopened Surface', (stage,model(stage)['name'])
 assert 'Reopened Surface' in c[stage]['native_material_names']
 assert c[stage]['texture_set_names'][0]=='Reopened Surface'
(a.output/'verified.json').write_text(json.dumps({'captures':d['captures'],'double_click':True,'escape_and_empty':True,'focus_out':True,'native_material':True,'undo_redo':True,'packed_reopen':True,'texture_set_f2':True,'shader_undo_redo':True},indent=2)+'\n');print('Native inline rename PASS')
