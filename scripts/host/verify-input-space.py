"""Per-usage OCIO conversion from original packed bytes, real GUI and GPU compositing."""
from parity_test_environment import private_display
import argparse,json,os,re,subprocess,math
from pathlib import Path
from PIL import Image,ImageChops,ImageStat
p=argparse.ArgumentParser();p.add_argument('--output',required=True,type=Path);p.add_argument('--check-only',action='store_true');p.add_argument('--cache',type=Path);a=p.parse_args();a.output=a.output.resolve();a.output.mkdir(parents=True,exist_ok=True);root=Path(__file__).resolve().parents[2]
env=dict(os.environ,LANG='sv_SE.UTF-8',LC_ALL='sv_SE.UTF-8',DISPLAY=private_display(),XDG_RUNTIME_DIR='/run/user/1000',XDG_CONFIG_HOME=str(a.output/'config'),XDG_CACHE_HOME=str((a.cache or a.output/'cache').resolve()),BTS_VERIFY_GPU_STACK='1',BTS_COLOR_SPACE_DEMO='1',BTS_FILL_TEXTURE_DEMO='1',BTS_SKEW_DEMO='1')
cmd=['bash',str(root/'scripts/host/run-host.sh'),'--workspace','--gui-fix-demo',str(a.output),'--gui-fix-mesh',str(root/'native/tests/fixtures/single_tile.obj'),'--gui-fix-resolution','128','--library-dir',str(a.output/'shelf'),'--recovery-dir',str(a.output/'recovery')]
if not a.check_only:
 with (a.output/'run.log').open('w') as log:
  proc=subprocess.Popen(cmd,env=env,stdout=log,stderr=subprocess.STDOUT)
  try:code=proc.wait(timeout=300)
  except subprocess.TimeoutExpired:proc.terminate();proc.wait(timeout=20);raise
 assert code==0,(code,(a.output/'run.log').read_text()[-3500:])
d=json.loads((a.output/'input-space.json').read_text());c={s['stage']:s for s in d['captures']};base=c['first-auto'];original=base['input_usages'][0];identity=original['layer'];linear=128/255;decoded=((linear+.055)/1.055)**2.4
if not a.check_only:assert d['pid']==proc.pid
for stage,s in c.items():
 assert s['document']['texture_sets'][0]['precision'][0]==8,stage
 assert s['decimal']=='.' and not s['shader_failures'] and s['source_obj_hash']==base['source_obj_hash'],stage
 assert 'ACEScg' in s['color_space_menu'] and 'Non-Color' in s['color_space_menu'] and len(s['color_space_menu'])>4,stage
 usages=s['input_usages'];assert all(x['asset']==original['asset'] and x['sha256']==original['sha256'] for x in usages),stage
 assert len(usages)==(1 if stage=='first-auto' else 2),stage
 bottom=next(x for x in usages if x['layer']==identity);assert bottom['space']=='' and max(abs(x-decoded) for x in bottom['pixel'][:3])<1e-4,stage
 override=stage in ['acescg','redone','non-color','restored-acescg','saved','opened-packed'];expected=linear if override else decoded
 for pixel in s['texture_probes']:assert max(abs(x-expected) for x in pixel[:3])<1e-4,(stage,pixel,expected)
 if stage!='first-auto':
  top=next(x for x in usages if x['layer']!=identity);assert top['space']==('Non-Color' if stage=='non-color' else 'ACEScg' if override else ''),stage
  assert max(abs(x-expected) for x in top['pixel'][:3])<1e-4,stage
 assert s['selected_space']==('Non-Color' if stage=='non-color' else 'ACEScg' if override else ''),stage
 if stage!='opened-packed':
  for field in ['scene_center','scene_distance','scene_yaw','scene_pitch','scene_projection','uv_center','uv_vertical_extent']:assert s['view'][field]==base['view'][field],(stage,field)
assert c['acescg']['undo_count']==c['two-auto']['undo_count']+1
assert c['undone']['document']==c['two-auto']['document'] and c['redone']['document']==c['acescg']['document']
assert c['reset-auto']['document']==c['two-auto']['document']
assert c['saved']['document']==c['opened-packed']['document'] and c['opened-packed']['source_file_absent']
assert c['saved']['composite_checked'] and c['saved']['composite_differences']==[]
before=Image.open(a.output/'two-auto-viewport.png').convert('RGB');after=Image.open(a.output/'acescg-viewport.png').convert('RGB');differences={}
for pane in base['view']['panes']:
 x,y,w,h=pane['rect'];box=(x+12,y+35,x+w-12,y+h-35);differences[pane['view']]=sum(ImageStat.Stat(ImageChops.difference(before.crop(box),after.crop(box))).mean)/765
assert differences['3D']>.005 and differences['2D']>.05,differences
pane=next(x for x in c['acescg']['view']['panes'] if x['view']=='2D');x,y,w,h=pane['rect'];flat=[value for u in [.15,.5,.85] for v in [.15,.5,.85] for value in after.getpixel((int(x+w*u),int(y+h*v)))[:3]];assert max(flat)-min(flat)<=1,flat
log=(a.output/'run.log').read_text();oracle=re.search(r'BTS_GPU_STACK_SELF_TEST cases=(\d+).*?=pass',log);assert oracle and int(oracle.group(1))>=81
assert f'BTS_SHUTDOWN clean pid={d["pid"]}' in log and not re.search(r'BTS_DRAW_FAILED|Shader compilation failed|BTS_SHARED_CONTEXT_FAILED',log,re.I)
result=dict(pid=d['pid'],locale='sv_SE.UTF-8',automatic_srgb_linear=decoded,explicit_acescg_linear=linear,appearance_difference=differences,one_usage_only=True,original_asset_bytes_shared=True,native_ocio_menu=True,uniform_unlit_2d_max_rgb_variation=max(flat)-min(flat),automatic_reset_and_single_undo=True,non_color_passthrough=True,cpu_final_composite_exact=True,gpu_preview_uses_override=True,gpu_resource_color_space_cache_oracle=True,packed_reopen_without_source=True,clean_shutdown=True)
(a.output/'verified.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
