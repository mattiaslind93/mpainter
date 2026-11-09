# SPDX-License-Identifier: GPL-3.0-or-later
"""Real Mode actions, shared settings/camera, render tabs and packed Cycles quality."""
from parity_test_environment import private_display
import argparse,copy,json,os,re,subprocess
from pathlib import Path
from PIL import Image,ImageChops,ImageStat
p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);p.add_argument('--check-only',action='store_true');p.add_argument('--cache',type=Path);a=p.parse_args();a.output=a.output.resolve();a.output.mkdir(parents=True,exist_ok=True);root=Path(__file__).resolve().parents[2]
env=dict(os.environ,LANG='sv_SE.UTF-8',LC_ALL='sv_SE.UTF-8',DISPLAY=private_display(),XDG_RUNTIME_DIR='/run/user/1000',XDG_CONFIG_HOME=str(a.output/'config'),XDG_CACHE_HOME=str((a.cache or a.output/'cache').resolve()),BTS_DISPLAY_LENS_DEMO='1',BTS_RENDER_WORKSPACE_DEMO='1',BTS_SHADER_DEMO='1',BTS_SKEW_DEMO='1');env.pop('BTS_CYCLES_PREVIEW_PROBE',None)
cmd=['bash',str(root/'scripts/host/run-host.sh'),'--workspace','--gui-fix-demo',str(a.output),'--gui-fix-mesh',str(root/'native/tests/fixtures/single_tile.obj'),'--gui-fix-resolution','128','--library-dir',str(a.output/'shelf'),'--recovery-dir',str(a.output/'recovery')]
if not a.check_only:
 with (a.output/'run.log').open('w') as log:
  proc=subprocess.Popen(cmd,env=env,stdout=log,stderr=subprocess.STDOUT)
  try:code=proc.wait(timeout=420)
  except subprocess.TimeoutExpired:proc.terminate();proc.wait(timeout=20);raise
 assert code==0,(code,(a.output/'run.log').read_text()[-4000:])
d=json.loads((a.output/'render-workspace.json').read_text());c={x['stage']:x for x in d['captures']};base=c['painting']
if not a.check_only:assert d['pid']==proc.pid
active=['rendered','light-rotated','raw32','post-color','post-restored','shader-tab','cpu','orbited','zoomed','orthographic','second-render','saved','opened-render']
for name,s in c.items():
 assert s['decimal']=='.' and not s['shader_failures'],name
 assert (name=='painted' or s['document']==base['document']) and s['source_obj_hash']==base['source_obj_hash'],name
 expected=copy.deepcopy(c['rendered' if name in ['painting','rendered'] else 'light-rotated']['environment'])
 if name not in ['painting','rendered','light-rotated','raw32']:
  expected['post_effects']['color_correction']=name=='post-color';expected['post_effects']['saturation']=0
 assert s['environment']==expected,name
 assert s['f9']=='F9' and s['f10']=='F10',name
 if name in active:
  assert s['render_ui'] and s['render']['active'] and s['render_panel_visible'],name
  assert not s['layers_visible'] and not s['shelf_visible'] and not s['display_floating'] and not s['shader_floating'],name
  assert s['render_tab_count']==2 and len(s['view']['panes'])==1,name
  assert 'Rendering Done' in s['render']['status'],(name,s['render'])
 else:
  assert not s['render_ui'] and not s['render']['active'] and not s['render_panel_visible'],name
  assert s['layers_visible'] and s['shelf_visible'] and len(s['view']['panes'])==2,name
assert c['rendered']['display_scroll_visible'] and c['shader-tab']['shader_name_visible']
assert c['rendered']['render']['device']=='CUDA' and c['cpu']['render']['device']=='CPU'
for name in ['raw32','cpu','saved','opened','opened-render']:
 assert c[name]['render']['samples']==32 and not c[name]['render']['denoise'],(name,c[name]['render'])
for field in ['scene_center','scene_distance','scene_yaw','scene_pitch','scene_projection']:
 assert c['rendered']['view'][field]==base['view'][field],field
 assert c['painting-restored']['view'][field]==c['orthographic']['view'][field],field
assert c['orbited']['view']['scene_yaw']!=c['cpu']['view']['scene_yaw']
assert c['zoomed']['view']['scene_distance']!=c['orbited']['view']['scene_distance']
assert 'Orthographic' in c['orthographic']['view']['scene_projection']
assert c['rendered']['render']['view_clip_start']==base['view']['clip']['near'] or abs(c['rendered']['render']['view_clip_start']-base['view']['clip']['near'])<1e-7
assert c['rendered']['render']['view_clip_end']==base['view']['clip']['far']
assert c['orthographic']['render']['view_clip_end']==2*c['orthographic']['view']['clip']['far']
assert c['painted']['hash']!=c['painting-restored']['hash'] and c['paint-undone']['hash']==c['painting-restored']['hash']
assert c['painted']['undo_count']==c['painting-restored']['undo_count']+1
raw_cuda=Image.open(a.output/'raw32-viewport.png').convert('RGB');raw_cpu=Image.open(a.output/'cpu-viewport.png').convert('RGB');device_diff=ImageChops.difference(raw_cuda,raw_cpu);device_max=max(hi for lo,hi in device_diff.getextrema());assert device_max<=2,device_max
for name in active:
 image=Image.open(a.output/(name+'-viewport.png')).convert('RGB');bright=sum(max(pixel)>10 for pixel in image.crop((20,45,image.width-20,image.height-45)).getdata());assert bright>10000,(name,bright)
assert c['painting-restored']['display_floating']==base['display_floating']
first=Image.open(a.output/'rendered-viewport.png').convert('RGB');second=Image.open(a.output/'light-rotated-viewport.png').convert('RGB');light_diff=sum(ImageStat.Stat(ImageChops.difference(first,second)).mean)/765;assert light_diff>.001,light_diff
raw=Image.open(a.output/'raw32-viewport.png').convert('RGB'); post=Image.open(a.output/'post-color-viewport.png').convert('RGB'); post_diff=sum(ImageStat.Stat(ImageChops.difference(raw,post)).mean)/765;assert post_diff>.001,post_diff
post_mean=ImageStat.Stat(post).mean;assert max(post_mean)-min(post_mean)<.01,post_mean
restored=Image.open(a.output/'post-restored-viewport.png').convert('RGB');restore_diff=sum(ImageStat.Stat(ImageChops.difference(raw,restored)).mean)/765;assert restore_diff<.005,restore_diff
assert c['post-color']['environment']['post_effects']['saturation']==0
log=(a.output/'run.log').read_text();assert f'BTS_SHUTDOWN clean pid={d["pid"]}' in log
assert not re.search(r'BTS_DRAW_FAILED|BTS_SHARED_CONTEXT_FAILED|Shader compilation failed|Failed to execute compilation',log,re.I)
result=dict(pid=d['pid'],locale='sv_SE.UTF-8',mode_actions_f9_f10=True,shared_camera_and_orbit_zoom_ortho=True,direct_clipping_preferences=True,paint_layout_restored=True,real_paint_stroke_undo_restored=True,existing_display_shader_tabs=True,cuda_and_cpu=True,cpu_cuda_max_rgb_level=device_max,live_hdri_rotation_difference=light_diff,native_cycles_post_color_difference=post_diff,native_post_restore_difference=restore_diff,scene_quality_packed_open=True,display_values_document_mesh_uv_retained=True,clean_shutdown=True)
(a.output/'verified.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
