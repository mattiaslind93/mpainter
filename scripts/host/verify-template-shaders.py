# SPDX-License-Identifier: GPL-3.0-or-later
"""File-New compatibility preset and real Save-As-Template/Browse roundtrip, own paths."""
from parity_test_environment import private_display
import argparse,json,os,re,subprocess
from pathlib import Path
from PIL import Image,ImageStat
p=argparse.ArgumentParser();p.add_argument('--output',required=True,type=Path);p.add_argument('--existing',action='store_true');a=p.parse_args();a.output=a.output.resolve();a.output.mkdir(parents=True,exist_ok=True);root=Path(__file__).resolve().parents[2]
if not a.existing:
 env=dict(os.environ,LANG='sv_SE.UTF-8',LC_ALL='sv_SE.UTF-8',BTS_TEMPLATE_SHADER_DEMO='1',BTS_SHADER_DEMO='1',BTS_SKEW_DEMO='1',QT_QPA_PLATFORM='offscreen',BTS_HEADLESS_WORKSPACE='1',DISPLAY=private_display(),XDG_RUNTIME_DIR='/run/user/1000',XDG_CONFIG_HOME=str(a.output/'config'),XDG_DATA_HOME=str(a.output/'data'),XDG_CACHE_HOME=str(a.output/'cache'),BTS_RECOVERY_DIR=str(a.output/'recovery'),BTS_SESSION_LOG_DIR=str(a.output/'logs'),BTS_TEMPLATE_DIR=str(a.output/'templates'))
 with (a.output/'run.log').open('w')as log:subprocess.run(['bash',str(root/'scripts/host/run-host.sh'),'--workspace','--gui-fix-demo',str(a.output),'--gui-fix-mesh',str(root/'native/tests/fixtures/single_tile.obj'),'--gui-fix-resolution','128','--library-dir',str(a.output/'shelf'),'--recovery-dir',str(a.output/'recovery')],env=env,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=300)
d=json.loads((a.output/'template-shaders.json').read_text());c={x['stage']:x for x in d['captures']}
def instance(stage):
 library=c[stage]['document']['shader_instances'];return next(v for v in library['instances']if v['id']==library['default'])
def profile(stage):return instance(stage)['parameters'].get('openpbr')
assert profile('builtin')['values']['coat_weight']==0
assert abs(profile('configured')['values']['coat_weight']-.6)<1e-6
for stage in ['template-loaded','opened','legacy-template']:
 assert instance(stage)['parameters']==instance('configured')['parameters'],stage
 assert profile(stage)and c[stage]['document']['texture_sets'][0]['layers'][0]['kind']=='paint'
 assert all(not l['paint_tiles']for s in c[stage]['document']['texture_sets']for l in s['layers'])
 for s in c[stage]['document']['texture_sets']:
  native=c[stage]['shader_nodes'][s['name']]['openpbr'];assert native['installed']and abs(native['sockets']['Coat Weight'][0]-.6)<1e-6
assert instance('configured')['id']!=instance('template-loaded')['id']!=instance('legacy-template')['id']
assert instance('template-loaded')==instance('opened')
assert profile('no-template')is None and profile('pbr')is None
assert all(not v['openpbr']['installed']for stage in ['no-template','pbr']for v in c[stage]['shader_nodes'].values())
template=json.loads((a.output/'templates/coated.btst').read_text());assert template['version']==2 and template['shader']=='openpbr-1.1-principled-compatibility'
assert all(x['decimal']=='.'and not x['shader_failures']for x in c.values())
for stage in c:
 im=Image.open(a.output/(stage+'.png')).convert('RGB');assert max(ImageStat.Stat(im).stddev)>2,stage
log=(a.output/'run.log').read_text();assert f'BTS_SHUTDOWN clean pid={d["pid"]}'in log and not re.search(r'BTS_DRAW_FAILED|Shader compilation failed|BTS_GPU_AUTHORING_CPU_READ|BTS_GPU_STACK_FALLBACK',log,re.I)
result={'pid':d['pid'],'stages':len(c),'template_version':2,'v1_read':True,'explicit_compatibility_builtin':True,'no_template_and_pbr_default_preserved':True,'real_save_as_template_and_browse':True,'shader_parameters_native_sockets_and_sharing_preserved':True,'project_local_shader_ids':True,'empty_layers_without_source_paint':True,'save_open':True,'own_template_directory':str(a.output/'templates'),'scope':'Project-template shader preservation and an optional portable compatibility preset; not modern no-template OpenPBR default or BSDF conformance.','clean_shutdown':True};(a.output/'verified.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
