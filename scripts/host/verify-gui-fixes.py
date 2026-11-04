"""Real File-New/paint regression on the linked Qt/Blender host, inside bts-build."""
from parity_test_environment import private_display
import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import statistics
import time
from PIL import Image, ImageChops

p=argparse.ArgumentParser()
p.add_argument('--output',type=Path,required=True)
p.add_argument('--resolution',type=int,default=512)
p.add_argument('--startup',action='store_true')
p.add_argument('--performance',action='store_true')
p.add_argument('--project',type=Path)
a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True)
root=Path(__file__).resolve().parents[2]
env=os.environ.copy();env.update(LANG='sv_SE.UTF-8',LC_ALL='sv_SE.UTF-8',DISPLAY=private_display(),XDG_RUNTIME_DIR='/run/user/1000')
if a.performance:env['BTS_GUI_PERFORMANCE']='1'
command=['bash',str(root/'scripts/host/run-host.sh'),'--workspace','--gui-fix-demo',str(a.output),'--gui-fix-mesh',str(root/'fixtures/single_tile.obj'),'--gui-fix-resolution',str(a.resolution),'--recovery-dir',str(a.output/'recovery')]
if a.project:command+=['--project',str(a.project)]
with (a.output/'run.log').open('w') as log:
    process=subprocess.Popen(command,stdout=log,stderr=subprocess.STDOUT,env=env)
    try:code=process.wait(timeout=300)
    except subprocess.TimeoutExpired:process.terminate();process.wait(timeout=15);raise
assert code==0,code
log=(a.output/'run.log').read_text();assert not re.search(r'(Shader compile failed|Shader compilation failed|error C\d+|BTS_DRAW_FAILED|BTS_HOST_FAILED)',log,re.I),log[-4000:]
demo=json.loads((a.output/'gui-fixes.json').read_text());captures={d['stage']:d for d in demo['captures']}
assert demo['pid']==process.pid
assert all(c['decimal']=='.' and c['shader_failures']=='' for c in captures.values())
assert demo['magenta_pixels']==0 and demo['post_release_ping_ms']<2000,demo
assert captures['created']['has_document'] and captures['created']['addPaintLayer_enabled'] and captures['created']['addFillLayer_enabled']
assert captures['before-stroke']['layers']==2
assert captures['painted']['hash']!=captures['before-stroke']['hash'] and captures['painted']['bump_hash']!=captures['before-stroke']['bump_hash']
if a.startup:
    before=captures['startup'];assert before['empty_view_visible'] and before['empty_layers_visible'] and 'Inget projekt' in before['empty_view_text'];assert not before['has_document'] and before['texture_sets']==[],before
    for name in ['addPaintLayer','addFillLayer']:
        assert not before[name+'_enabled'] and before[name+'_tooltip']=='Inget projekt öppet',before
    assert 'FixtureSurface' in captures['created']['texture_sets'] and 'Material' not in captures['created']['texture_sets']
if a.performance:
    assert a.resolution==1024 and demo['performance'] and demo['mouse_moves']>=60 and demo['roughness_value_changes']>=100
    assert demo['stroke_max_ping_ms']<100 and demo['roughness_max_ping_ms']<100,demo
    assert demo['roughness_release_ms']<200 and abs(demo['roughness_release_value']-.8)<1e-6,demo
    before=captures['roughness-before'];after=captures['roughness-after']
    assert abs(after['roughness']-.8)<1e-6 and after['undo_count']==before['undo_count']+1 and demo['roughness_release_undo_count']==before['undo_count']+1,(before,after,demo)
    assert demo['padding_before_stroke']==demo['padding_before_release'],demo
    times=[float(t) for t in re.findall(r'BTS_STROKE_MS wall_ms=([0-9.]+)',log)];assert len(times)>=8 and statistics.median(times)<8,times
    for key in ['paint-reference-checked','reference-checked']:
        reference=captures[key];assert reference['composite_checked'] and not reference['composite_differences'],reference
    before_image=Image.open(a.output/'before-stroke-framebuffer.png').convert('RGB');painted=Image.open(a.output/'painted-framebuffer.png').convert('RGB');difference=ImageChops.difference(before_image,painted);changed=sum(max(rgb)>15 for rgb in difference.getdata());assert changed>=25,changed
assert f'BTS_SHUTDOWN clean pid={process.pid}' in log
result={'pid':process.pid,'locale':'sv_SE.UTF-8','resolution':a.resolution,'gui_new_and_bump_stroke':True,'zero_magenta_pixels':True,'post_release_ping_ms':demo['post_release_ping_ms'],'startup_empty_view_verified':a.startup,'clean_exit':True,'input':demo['input']}
if a.performance:result['performance']={'mouse_moves':demo['mouse_moves'],'roughness_changes':demo['roughness_value_changes'],'stroke_max_ping_ms':demo['stroke_max_ping_ms'],'roughness_max_ping_ms':demo['roughness_max_ping_ms'],'roughness_release_ms':demo['roughness_release_ms'],'single_roughness_undo':True,'padding_calls_during_stroke':demo['padding_before_release']-demo['padding_before_stroke'],'median_stroke_ms':statistics.median(times),'dab_groups':len(times),'changed_framebuffer_pixels':changed,'async_final_composite_matches_sync_reference':True}
(a.output/'verified.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
