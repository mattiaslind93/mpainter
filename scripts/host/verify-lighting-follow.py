"""Native world/view lighting comparison, explicit project setting and legacy Open."""
from parity_test_environment import private_display
import argparse
import json
import os
from pathlib import Path
import re
import subprocess
from PIL import Image,ImageChops,ImageStat

p=argparse.ArgumentParser()
p.add_argument('--output',type=Path,required=True)
p.add_argument('--legacy-project',type=Path,required=True)
a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True)
assert a.legacy_project.is_file()
root=Path(__file__).resolve().parents[2]
env=dict(os.environ,LANG='sv_SE.UTF-8',LC_ALL='sv_SE.UTF-8',DISPLAY=private_display(),XDG_RUNTIME_DIR='/run/user/1000',BTS_LIGHTING_DEMO='1',BTS_LEGACY_LIGHTING_PROJECT=str(a.legacy_project))
command=['bash',str(root/'scripts/host/run-host.sh'),'--workspace','--gui-fix-demo',str(a.output),'--gui-fix-mesh',str(root/'native/tests/fixtures/single_tile.obj'),'--gui-fix-resolution','512','--recovery-dir',str(a.output/'recovery')]
with (a.output/'run.log').open('w') as log:
    process=subprocess.Popen(command,env=env,stdout=log,stderr=subprocess.STDOUT)
    try:code=process.wait(timeout=300)
    except subprocess.TimeoutExpired:process.terminate();process.wait(timeout=15);raise
assert code==0,code
demo=json.loads((a.output/'lighting.json').read_text());assert demo['pid']==process.pid
c={item['stage']:item for item in demo['captures']}
for stage in ['startup','created','follow0','follow-orbit90','follow-rotation45','undo-alignment','opened-follow']:
    assert c[stage]['environment']['lighting_follows_view'] and c[stage]['checkbox'],(stage,c[stage])
for stage in ['world0','world-orbit90','world-rotation45','undo-rotation','redo-alignment','opened-world']:
    assert not c[stage]['environment']['lighting_follows_view'] and not c[stage]['checkbox'],stage
for stage in ['opened-world','opened-follow']:assert abs(c[stage]['environment']['rotation']-30)<1e-4
assert c['legacy-opened']['has_document']
assert c['legacy-opened']['environment']['alignment']=='World' # Pre-F C roundtrip fixture's saved native flag, despite the new follow default.
assert abs(c['undo-rotation']['environment']['rotation'])<1e-4
assert c['undo-alignment']['environment']['lighting_follows_view']
assert c['redo-alignment']['environment']['alignment']=='World'
def background_difference(first,second):
    pane=next(p['rect'] for p in c[first]['view']['panes'] if p['view']=='3D')
    x,y,w,h=map(int,pane)
    # A thin strip at the scene pane's left edge excludes the mesh and HUD.
    box=(x+12,y+60,x+30,y+h-45)
    image=Image.open(a.output/(first+'.png')).convert('RGB').crop(box)
    other=Image.open(a.output/(second+'.png')).convert('RGB').crop(box)
    return sum(ImageStat.Stat(ImageChops.difference(image,other)).mean)/(3*255)
follow=background_difference('follow0','follow-orbit90')
world=background_difference('world0','world-orbit90')
rotations={kind:background_difference(kind+'-orbit90',kind+'-rotation45') for kind in ['follow','world']}
assert follow<.005,follow
assert world>.005 and min(rotations.values())>.005,(world,rotations)
assert c['follow0']['view']['scene_view_matrix']!=c['follow-orbit90']['view']['scene_view_matrix']
assert c['world0']['view']['scene_view_matrix']!=c['world-orbit90']['view']['scene_view_matrix']
assert abs(abs(c['follow0']['view']['scene_yaw']-c['follow-orbit90']['view']['scene_yaw'])-90)<1e-3
assert all(item['decimal']=='.' and not item['shader_failures'] for item in c.values())
log=(a.output/'run.log').read_text();assert not re.search(r'Shader compil|error C\d+|BTS_DRAW_FAILED|BTS_HOST_FAILED',log,re.I),log[-4000:]
assert f'BTS_SHUTDOWN clean pid={process.pid}' in log
result=dict(pid=process.pid,locale='sv_SE.UTF-8',startup_and_new_follow_default=True,orbit_degrees=90,follow_background_difference=follow,world_background_difference=world,rotation_differences=rotations,undo_redo=True,save_open_both_modes=True,legacy_native_mode_retained=True,clean_exit=True)
(a.output/'verified.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
