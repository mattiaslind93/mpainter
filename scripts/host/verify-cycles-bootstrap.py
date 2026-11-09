# SPDX-License-Identifier: GPL-3.0-or-later
"""Native Qt/Cycles CPU and CUDA display, with an EEVEE camera/geometry oracle."""
from parity_test_environment import private_display
import argparse,json,os,re,subprocess
from collections import Counter
from pathlib import Path
from PIL import Image,ImageChops,ImageStat
p=argparse.ArgumentParser();p.add_argument('--output',required=True,type=Path);p.add_argument('--check-only',action='store_true');a=p.parse_args();a.output=a.output.resolve();a.output.mkdir(parents=True,exist_ok=True);root=Path(__file__).resolve().parents[2];results={};images={}
for mode in ['eevee','cuda','cpu']:
 folder=a.output/mode;folder.mkdir(parents=True,exist_ok=True)
 env=dict(os.environ,LANG='sv_SE.UTF-8',LC_ALL='sv_SE.UTF-8',DISPLAY=private_display(),XDG_RUNTIME_DIR='/run/user/1000',XDG_CONFIG_HOME=str(folder/'config'),XDG_CACHE_HOME=str(a.output/'cache'),BTS_RENDER_MODE_DEMO='1',BTS_SHADER_DEMO='1',BTS_SKEW_DEMO='1')
 env.pop('BTS_CYCLES_PREVIEW_PROBE',None);env.pop('BTS_CYCLES_PIXEL_PROBE',None)
 if mode!='eevee':env.update(BTS_CYCLES_PREVIEW_PROBE='1',BTS_CYCLES_DEVICE='CPU' if mode=='cpu' else 'CUDA')
 cmd=['bash',str(root/'scripts/host/run-host.sh'),'--workspace','--gui-fix-demo',str(folder),'--gui-fix-mesh',str(root/'native/tests/fixtures/single_tile.obj'),'--gui-fix-resolution','128','--library-dir',str(folder/'shelf'),'--recovery-dir',str(folder/'recovery')]
 if not a.check_only:
  with (folder/'run.log').open('w') as log:
   proc=subprocess.Popen(cmd,env=env,stdout=log,stderr=subprocess.STDOUT)
   try:code=proc.wait(timeout=480)
   except subprocess.TimeoutExpired:proc.terminate();proc.wait(timeout=20);raise
  assert code==0,(mode,code,(folder/'run.log').read_text()[-3000:])
 data=json.loads((folder/'render-mode.json').read_text());pid=data['pid'];assert len(data['captures'])==3
 if not a.check_only:assert pid==proc.pid
 baseline=data['captures'][0]
 for s in data['captures']:
  assert s['has_document'] and s['decimal']=='.' and not s['shader_failures'],mode
  assert s['document']==baseline['document'] and s['source_obj_hash']==baseline['source_obj_hash'] and s['view']==baseline['view'],mode
 log=(folder/'run.log').read_text();assert f'BTS_SHUTDOWN clean pid={pid}' in log
 assert 'BTS_GL_ERROR' not in log,(mode,'OpenGL error during context handoff')
 assert not re.search(r'QPainter::begin\(\):.*context|Painter not active',log),(mode,'Capture changed the viewport GL context')
 assert not re.search(r'BTS_SHARED_CONTEXT_FAILED|Shader compilation failed|BTS_DRAW_FAILED|Failed to execute compilation|CUDA error|BTS_CYCLES_PIXEL',log,re.I),(mode,log[-2000:])
 if mode!='eevee':
  assert f'BTS_CYCLES_MODE same_pid={pid}' in log and 'BTS_CYCLES_FRAME engine=CYCLES info=Rendering Done' in log,mode
  assert 'BTS_CYCLES_DEVICE '+('CPU' if mode=='cpu' else 'GPU') in log,mode
  if mode=='cuda':assert 'NVIDIA GeForce RTX 3090' in log,log[-3000:]
 image=Image.open(folder/'render-62.png').convert('RGB');images[mode]=image
 points=[(x,y) for y in range(35,image.height-40) for x in range(20,image.width-20) if (lambda c:c[2]>c[0]+12 and c[1]>c[0]+8)(image.getpixel((x,y)))]
 assert len(points)>10000,(mode,len(points))
 # Reject small HUD/axis glyphs; compare the dense model silhouette.
 columns=Counter(x for x,y in points);rows=Counter(y for x,y in points);xs=[x for x,n in columns.items() if n>32];ys=[y for y,n in rows.items() if n>32]
 box=(min(xs),min(ys),max(xs)+1,max(ys)+1)
 results[mode]=dict(pid=pid,opengl_errors=0,visible_model_pixels=len(points),box=box,source_hash=baseline['source_obj_hash'],view=baseline['view'])
 print(mode,pid,box,flush=True)
base=results['eevee']['box']
for mode in ['cuda','cpu']:
 assert results[mode]['source_hash']==results['eevee']['source_hash']
 for key in ['scene_center','scene_yaw','scene_pitch','scene_distance','scene_projection']:assert results[mode]['view'][key]==results['eevee']['view'][key],(mode,key)
 assert max(abs(x-y) for x,y in zip(results[mode]['box'],base))<7,(mode,results[mode]['box'],base)
cpu=ImageStat.Stat(images['cpu'].crop(base)).mean;cuda=ImageStat.Stat(images['cuda'].crop(base)).mean
mean_error=max(abs(x-y) for x,y in zip(cpu,cuda))/255;assert mean_error<.1,(mean_error,cpu,cuda)
difference=ImageChops.difference(images['cpu'],images['cuda']);pixel_mae=sum(ImageStat.Stat(difference).mean)/765;maximum=max(x[1] for x in difference.getextrema());assert maximum<=3,(pixel_mae,maximum)
result=dict(native_single_process=True,locale='sv_SE.UTF-8',modes={name:{k:v for k,v in data.items() if k not in ['view','source_hash']} for name,data in results.items()},source_hash=results['eevee']['source_hash'],cpu_cuda_mean_error=mean_error,cpu_cuda_pixel_mae=pixel_mae,cpu_cuda_max_display_levels=maximum,camera_geometry_matches_eevee=True,document_unchanged=True,production_mode_ui_pending=True)
(a.output/'verified.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps({k:v for k,v in result.items() if k!='modes'},indent=2))
