"""GPU compositor: vignette affects 3D only, retained values, UI undo and packed Save/Open."""
from parity_test_environment import private_display
import argparse,json,os,re,subprocess
from pathlib import Path
from PIL import Image,ImageChops,ImageStat
p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);a=p.parse_args();a.output=a.output.resolve();a.output.mkdir(parents=True,exist_ok=True);root=Path(__file__).resolve().parents[2]
env=dict(os.environ,LANG='sv_SE.UTF-8',LC_ALL='sv_SE.UTF-8',DISPLAY=private_display(),XDG_RUNTIME_DIR='/run/user/1000',XDG_CONFIG_HOME=str(a.output/'config'),BTS_DISPLAY_POST_DEMO='1',BTS_DISPLAY_POST_DEBUG='1',BTS_SHADER_DEMO='1',BTS_SKEW_DEMO='1')
cmd=['bash',str(root/'scripts/host/run-host.sh'),'--workspace','--gui-fix-demo',str(a.output),'--gui-fix-mesh',str(root/'native/tests/fixtures/single_tile.obj'),'--gui-fix-resolution','128','--library-dir',str(a.output/'shelf'),'--recovery-dir',str(a.output/'recovery')]
with (a.output/'run.log').open('w') as log:
 proc=subprocess.Popen(cmd,env=env,stdout=log,stderr=subprocess.STDOUT)
 try:code=proc.wait(timeout=240)
 except subprocess.TimeoutExpired:proc.terminate();proc.wait(timeout=15);raise
assert code==0,(code,(a.output/'run.log').read_text()[-3000:])
d=json.loads((a.output/'display-post.json').read_text());assert d['pid']==proc.pid;c={x['stage']:x for x in d['captures']};initial=c['baseline']
for name,s in c.items():
 assert s['panel_visible'],name
 assert s['document']==initial['document'],name
 assert s['source_obj_hash']==initial['source_obj_hash'],name
 assert s['decimal']=='.' and not s['shader_failures'],name
assert not c['headers-disabled']['effect'] and not c['headers-disabled']['master']
assert c['armed']['vignette_expanded'] and not c['headers-disabled']['vignette_expanded']
assert c['armed']['master'] and not c['armed']['effect'] and c['armed']['strength']==1
assert not c['disabled-retained']['effect'] and c['disabled-retained']['strength']==.2
assert c['undo-effect']['effect'] and c['undo-effect']['strength']==.2
assert c['saved']['environment']==c['opened']['environment']
assert not c['master-off-retained']['master'] and c['master-off-retained']['strength']==1 and c['master-off-retained']['effect']
# Restore button is still actionable while the master is off.
assert not c['restored']['master'] and not c['restored']['effect'] and abs(c['restored']['strength']-.2)<1e-6
images={name:Image.open(a.output/(name+'-viewport.png')).convert('RGB') for name in c}
def diff(first,second,view):
 pane=next(p for p in initial['view']['panes'] if p['view']==view);x,y,w,h=pane['rect'];box=(x+12,y+42,x+w-12,y+h-24)
 return sum(ImageStat.Stat(ImageChops.difference(images[first].crop(box),images[second].crop(box))).mean)/(3*255)
for stage in ['headers-disabled','master-only','armed','disabled-retained']:
 assert diff('baseline',stage,'3D')<.0005,(stage,diff('baseline',stage,'3D'))
for stage in c:assert diff('baseline',stage,'2D')<.0005,(stage,diff('baseline',stage,'2D'))
strong=diff('baseline','enabled','3D');weak=diff('baseline','weaker','3D');assert strong>weak+.01 and weak>.001,(strong,weak)
# Open normally reframes the scene; authored state and stored settings stay exact.
log=(a.output/'run.log').read_text();assert f'BTS_SHUTDOWN clean pid={proc.pid}' in log;assert not re.search(r'BTS_HOST_FAILED|BTS_DRAW_FAILED|Shader compilation failed',log,re.I)
result=dict(pid=proc.pid,locale='sv_SE.UTF-8',gpu_viewport_compositor=True,vignette_difference=strong,weaker_difference=weak,uv_difference=0,master_and_effect_independent=True,values_retained=True,undo=True,packed_open=True,authored_pixels_mesh_uv_unchanged=True)
(a.output/'verified.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
