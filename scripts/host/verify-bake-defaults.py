"""GUI-New bake defaults, live disabled reasons, native maps, Undo and saved preferences."""
from parity_test_environment import private_display
import argparse,json,os,re,subprocess
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True);root=Path(__file__).resolve().parents[2]
env=dict(os.environ,LANG='sv_SE.UTF-8',LC_ALL='sv_SE.UTF-8',DISPLAY=private_display(),XDG_RUNTIME_DIR='/run/user/1000',BTS_BAKE_FIX_DEMO='1')
command=['bash',str(root/'scripts/host/run-host.sh'),'--workspace','--gui-fix-demo',str(a.output),'--gui-fix-mesh',str(root/'native/tests/fixtures/single_tile.obj'),'--gui-fix-resolution','512','--recovery-dir',str(a.output/'recovery')]
with (a.output/'run.log').open('w') as log:
 process=subprocess.Popen(command,env=env,stdout=log,stderr=subprocess.STDOUT)
 try:code=process.wait(timeout=300)
 except subprocess.TimeoutExpired:process.terminate();process.wait(timeout=15);raise
assert code==0,code
proof=json.loads((a.output/'bake-fix.json').read_text());assert proof['pid']==process.pid
c={v['stage']:v for v in proof['captures']};d={v['stage']:v for v in proof['dialogs']}
assert d['default']['enabled'] and d['default']['project_mesh'] and d['default']['source_visible']
for stage,word in [('no-source','high-poly'),('no-maps','mesh map'),('no-tiles','texture set/tile'),('no-cage','cage file'),('saved-explicit-source-off','high-poly')]:
 assert not d[stage]['enabled'] and word in d[stage]['tooltip'] and d[stage]['tooltip']==d[stage]['help'],d[stage]
assert not d['saved-explicit-source-off']['project_mesh'] and d['ready-ao-curvature']['enabled']
assert not c['preferences-opened']['bake_configuration']['project_mesh']
assert c['preferences-opened']['bake_configuration']==c['settings-saved']['bake_configuration']
assert {v['role'] for v in c['baked']['mesh_maps']}=={3,4}
assert not c['undo']['mesh_maps']
assert c['baked']['mesh_maps']==c['redo']['mesh_maps']==c['maps-opened']['mesh_maps']
assert c['baked']['undo_count']==c['preferences-opened']['undo_count']+1
assert all(v['decimal']=='.' and not v['shader_failures'] for v in c.values())
log=(a.output/'run.log').read_text();assert not re.search(r'Shader compilation failed|error C\d+|BTS_DRAW_FAILED|BTS_HOST_FAILED',log,re.I),log[-4000:]
assert f'BTS_SHUTDOWN clean pid={process.pid}' in log
result=dict(pid=process.pid,locale='sv_SE.UTF-8',project_mesh_default=True,source_visible_without_scroll=True,live_disabled_reasons=['high-poly','maps','texture sets/tiles','cage'],saved_explicit_source_off=True,baked_maps=['AO','Curvature'],resolution=512,undo_redo=True,save_open=True,clean_exit=True)
(a.output/'verified.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
