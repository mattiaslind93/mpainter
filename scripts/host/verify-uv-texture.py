"""Compare presented UV colors to canonical GPU channel samples; private offscreen GL."""
from parity_test_environment import private_display
import argparse,json,os,re,subprocess
from pathlib import Path
from PIL import Image,ImageStat
p=argparse.ArgumentParser();p.add_argument('--output',required=True,type=Path);p.add_argument('--existing',action='store_true');a=p.parse_args();a.output=a.output.resolve();a.output.mkdir(parents=True,exist_ok=True);root=Path(__file__).resolve().parents[2]
mesh=a.output/'uv_quad.obj';mesh.write_text('mtllib uv_quad.mtl\no UVQuad\nv -1 -1 0\nv 1 -1 0\nv 1 1 0\nv -1 1 0\nvt 0 0\nvt 1 0\nvt 1 1\nvt 0 1\nusemtl FixtureSurface\nf 1/1 2/2 3/3 4/4\n');mesh.with_suffix('.mtl').write_text('newmtl FixtureSurface\nKd 1 1 1\nPr 0.5\nPm 0\n')
if not a.existing:
 env=dict(os.environ,LANG='sv_SE.UTF-8',LC_ALL='sv_SE.UTF-8',BTS_UV_TEXTURE_DEMO='1',QT_QPA_PLATFORM='offscreen',BTS_HEADLESS_WORKSPACE='1',DISPLAY=private_display(),XDG_RUNTIME_DIR='/run/user/1000',XDG_CONFIG_HOME=str(a.output/'config'),XDG_DATA_HOME=str(a.output/'data'),XDG_CACHE_HOME=str(a.output/'cache'),BTS_RECOVERY_DIR=str(a.output/'recovery'),BTS_SESSION_LOG_DIR=str(a.output/'logs'))
 with (a.output/'run.log').open('w') as log:subprocess.run(['bash',str(root/'scripts/host/run-host.sh'),'--workspace','--gui-fix-demo',str(a.output),'--gui-fix-mesh',str(mesh),'--gui-fix-resolution','512','--library-dir',str(a.output/'shelf'),'--recovery-dir',str(a.output/'recovery')],env=env,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=240)
proof=json.loads((a.output/'uv-texture.json').read_text());caps={c['stage']:c for c in proof['captures']};errors={};pixels={}
for name,capture in caps.items():
 assert not capture['shader_failures'];assert not capture['probes']or len(capture['probes'])>=4;image=Image.open(a.output/(name+'-viewport.png')).convert('RGB');assert max(ImageStat.Stat(image).stddev)>2,('blank framebuffer',name);stage_error=0;pixels[name]=[]
 for sample in capture['probes']:
  point=sample['x'],sample['y'];assert 0<=point[0]<image.width and 0<=point[1]<image.height
  actual=image.getpixel(point);expected=[255*max(0,min(1,v)) for v in sample['expected']];error=max(abs(x-y)/255 for x,y in zip(actual,expected));stage_error=max(stage_error,error);pixels[name].append(actual)
 errors[name]=stage_error;assert stage_error<4/255,(name,stage_error,[(s,image.getpixel((s['x'],s['y'])))for s in capture['probes']])
assert caps['gray']['contours']==4,caps['gray']['contours']
assert pixels['color']!=pixels['gray']
assert max(max(p)-min(p)for p in pixels['color'])>20
assert all(max(abs(c-51)for c in p)<=2 for p in pixels['roughness']),pixels['roughness']
assert caps['gray']['environment']!=caps['gray-light90']['environment']
assert pixels['gray']==pixels['gray-light90'],'Lighting changed unlit UV pixels'
assert max(max(p[c]for p in pixels['pattern'])-min(p[c]for p in pixels['pattern'])for c in range(3))>20
for name,order in {'gray':['2D'],'color':['2D'],'roughness':['2D'],'pattern':['2D'],'split':['3D','2D'],'swapped':['2D','3D'],'3d-only':['3D'],'3d-swap':['3D'],'2d-restored':['2D'],'2d-swap':['2D'],'split-restored':['2D','3D']}.items():assert [p['view'] for p in caps[name]['view']['panes']]==order,(name,caps[name]['view'])
for name in ['color-solo','roughness']:assert caps[name]['document']==caps['color']['document'],name
for name in ['split','swapped','3d-only','3d-swap','2d-restored','2d-swap','split-restored']:assert caps[name]['document']==caps['pattern']['document'],name
log=(a.output/'run.log').read_text();assert f'BTS_SHUTDOWN clean pid={proof["pid"]}' in log;assert not re.search(r'BTS_DRAW_FAILED|Shader compilation failed|BTS_GPU_STACK_FALLBACK|BTS_GPU_AUTHORING_CPU_READ',log,re.I)
result={'pid':proof['pid'],'stages':len(caps),'probe_count':sum(len(c['probes'])for c in caps.values()),'max_rgb_error':max(errors.values()),'stage_errors':errors,'gray_unlit_lighting_invariant':True,'color_material_and_solo':True,'roughness_raw':True,'nonuniform_gpu_texture':True,'f1_f2_f3_f4':True,'layout_and_inspection_do_not_edit_document':True,'clean_shutdown':True};(a.output/'verified.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
