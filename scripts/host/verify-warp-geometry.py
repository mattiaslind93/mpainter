"""PD145: native whole-grid BVH fit, authoring retention and packed persistence."""
from parity_test_environment import private_display
import argparse,json,math,os,re,subprocess
from pathlib import Path
from PIL import Image,ImageDraw
p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);p.add_argument('--cache',type=Path);p.add_argument('--check-only',action='store_true');p.add_argument('--gpu',action='store_true');a=p.parse_args();a.output=a.output.resolve();a.output.mkdir(parents=True,exist_ok=True);root=Path(__file__).resolve().parents[2]
if not a.check_only:
 n=25;lines=['mtllib curved.mtl','o CurvedSheet','usemtl FixtureSurface','s 1']
 for j in range(n):
  for i in range(n):
   x=-2+4*i/(n-1);y=-2+4*j/(n-1);lines.append(f'v {x:.12g} {y:.12g} {.2*x*x+.08*y*y:.12g}')
 for j in range(n):
  for i in range(n):lines.append(f'vt {i/(n-1):.12g} {j/(n-1):.12g}')
 for j in range(n-1):
  for i in range(n-1):
   k=j*n+i+1;ids=[k,k+1,k+n+1,k+n];lines.append('f '+' '.join(f'{q}/{q}'for q in ids))
 (a.output/'curved.obj').write_text('\n'.join(lines)+'\n');(a.output/'curved.mtl').write_text('newmtl FixtureSurface\nKd 1 1 1\nPr .5\nPm 0\n')
 image=Image.new('RGB',(32,32),(30,80,210));draw=ImageDraw.Draw(image);draw.polygon([(3,8),(18,8),(18,2),(29,16),(18,29),(18,23),(3,23)],fill=(235,90,20));draw.rectangle((3,2,8,5),fill=(40,220,70));image.save(a.output/'decal.png')
 env=dict(os.environ,LANG='sv_SE.UTF-8',LC_ALL='sv_SE.UTF-8',DISPLAY=private_display(),XDG_RUNTIME_DIR='/run/user/1000',XDG_CONFIG_HOME=str(a.output/'config'),XDG_DATA_HOME=str(a.output/'data'),XDG_CACHE_HOME=str((a.cache or a.output/'cache').resolve()),BTS_WARP_GEOMETRY_DEMO=str(a.output))
 if a.gpu:env.update(BTS_VERIFY_GPU_STACK='1',BTS_GPU_DENSE_ORACLE='1')
 with (a.output/'run.log').open('w')as log:
  process=subprocess.Popen(['bash',str(root/'scripts/host/run-host.sh'),'--workspace','--library-dir',str(a.output/'shelf'),'--recovery-dir',str(a.output/'recovery')],env=env,stdout=log,stderr=subprocess.STDOUT)
  try:code=process.wait(timeout=400)
  except subprocess.TimeoutExpired:process.terminate();process.wait(timeout=20);raise
 assert code==0,(code,(a.output/'run.log').read_text()[-6000:])
d=json.loads((a.output/'warp-geometry.json').read_text());c={s['stage']:s for s in d['captures']};assert len(c)==18,list(c)
if not a.check_only:assert d['pid']==process.pid
def g(stage):return c[stage]['warp_geometry']
def hash_(stage):return c[stage]['textures'][0]['color_hash']
def close(left,right,tol=1e-8):
 if isinstance(left,list):return len(left)==len(right)and all(close(x,y,tol)for x,y in zip(left,right))
 return abs(left-right)<=tol
assert c['flat']['toggle_visible'] and not g('flat')['enabled']
assert g('fitted')['enabled'] and g('fitted')['fitted'] and g('fitted')['hits']==9 and g('fitted')['misses']==0,g('fitted')
assert hash_('flat')!=hash_('fitted')
# The opaque decal must have no holes where the fitted patch crosses source triangles.
interior=[v for v in c['fitted']['textures'][0]['samples']if v['x']in(48,64,79)and v['y']in(48,64,79)]
assert len(interior)==9 and all(v['rgba'][3]>.999 for v in interior),interior
for left,right in [('flat','fitted'),('fitted','moved'),('deformed','moved-deformed'),('view-edited','retained')]:assert close(g(left)['author'],g(right)['author']),(left,right)
assert not close(g('moved')['author'],g('deformed')['author'])
assert not close(g('moved-deformed')['author'],g('view-edited')['author'])
assert not close(g('fitted')['local'],g('moved')['local'])
for left,right in [('retained','off'),('off-moved','undo'),('resumed','redo'),('saved','opened'),('off-saved','off-opened'),('opened','reopened-resume')]:
 assert close(g(left)['world'],g(right)['world'],1e-7),(left,right,'grid');assert hash_(left)==hash_(right),(left,right,'pixels')
assert not g('off')['enabled'] and close(g('off')['local'],g('off-moved')['local'])
assert close(g('retained')['author'],g('resumed')['author'],1e-7)
for left,right in [('saved','opened'),('off-saved','off-opened')]:assert c[left]['document']==c[right]['document'],(left,right,'document')
assert c['fitted']['undo']==c['flat']['undo']+1
surface_error=0
for stage in ['fitted','moved','deformed','moved-deformed','retained','resumed','reopened-resume']:
 assert g(stage)['fitted'],stage
 for authored,world in zip(g(stage)['author'],g(stage)['world']):
  x,y,z=world;expected=-.2*x*x-.08*z*z+(authored[1]+.5)*1.6;surface_error=max(surface_error,abs(y-expected))
assert surface_error<.007,surface_error
for stage,state in c.items():
 assert state['source_geometry_hash']==c['flat']['source_geometry_hash'],stage
 for key in ['scene_center','scene_yaw','scene_pitch','scene_distance']:assert state['view'][key]==c['flat']['view'][key],(stage,key)
 assert not state['shader_failures'],stage
 assert state['textures'][0]['cpu_reference_error']<1e-4,(stage,state['textures'][0]['cpu_reference_error'])
 image=Image.open(a.output/(stage+'-viewport.png')).convert('RGB');assert not any(r>235 and green<20 and b>235 for r,green,b in image.getdata()),stage
log=(a.output/'run.log').read_text();assert 'BTS_WARP_FIT_MS' in log and 'BTS_SHUTDOWN clean' in log
assert not re.search(r'Shader compilation failed|error C\d+|BTS_DRAW_FAILED|BTS_HOST_FAILED|BTS_FINALIZE_FALLBACK',log,re.I),log[-4000:]
assert not(a.output/'decal.png').exists()
proof={'pid':d['pid'],'locale':'sv_SE.UTF-8','stages':list(c),'max_piecewise_surface_error':surface_error,'author_and_view_edits_retained':True,'freeze_off_resume':True,'one_undo_per_toggle':True,'packed_on_off_open_without_original':True,'source_geometry_uv_camera_unchanged':True,'max_cpu_reference_error':max(v['textures'][0]['cpu_reference_error']for v in c.values()),'opaque_interior_samples':len(interior),'magenta_pixels':0}
if a.gpu:
 probes=re.findall(r'BTS_GPU_STACK_VERIFIED channel=(\d+) probes=64 max_error=(\S+) valid=(\d+)',log)
 assert probes and all(p[2]=='1' for p in probes)
 assert 'BTS_GPU_WARP_VERIFIED' in log and 'BTS_GPU_STACK_FALLBACK' not in log and 'BTS_GPU_CPU_FALLBACK' not in log
 proof['gpu']={'probes':len(probes),'maximum_error':max(float(p[1]) for p in probes),'fallbacks':0}
 native=re.findall(r'BTS_NATIVE_GPU_ORACLE channels=(\d+) probes=(\d+) tiles=(\d+) max_error=(\S+) full_resolution=1',log)
 assert len(native)==len(c) and all(int(v[1])>=128*128 for v in native),(len(native),len(c))
 proof['gpu']['actual_native_images']={'captures':len(native),'texels_checked':sum(int(v[1])for v in native),'maximum_error':max(float(v[3])for v in native),'full_resolution':True}
(a.output/'verified.json').write_text(json.dumps(proof,indent=2)+'\n');print(json.dumps(proof,indent=2))
