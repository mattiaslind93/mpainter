"""PD143 native hard-surface projection: measured boundary alignment, distortion and preservation."""
from parity_test_environment import private_display
import argparse,json,math,os,subprocess
from pathlib import Path
from PIL import Image
p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);p.add_argument('--cache',type=Path);p.add_argument('--check-only',action='store_true');a=p.parse_args();a.output=a.output.resolve();a.output.mkdir(parents=True,exist_ok=True);root=Path(__file__).resolve().parents[2]
# Original mechanical L-profile, extruded along Z. Concave caps and six side quads.
profile=[(0,0),(3,0),(3,1),(1,1),(1,2),(0,2)];vertices=[(x,y,z)for z in (0,.6)for x,y in profile];faces=[list(reversed(range(6))),list(range(6,12))]+[[i,(i+1)%6,(i+1)%6+6,i+6]for i in range(6)]
header='mtllib mechanical.mtl\no MechanicalBracket\nusemtl FixtureSurface\n'+''.join('v %g %g %g\n'%v for v in vertices)
(a.output/'mechanical.mtl').write_text('newmtl FixtureSurface\nKd 0.65 0.65 0.65\nPr 0.4\nPm 0\n')
(a.output/'mechanical.obj').write_text(header+''.join('f '+' '.join(str(i+1)for i in face)+'\n'for face in faces))
uv=[];uv_faces=[]
for n,face in enumerate(faces):
 axis=min(range(3),key=lambda c:max(vertices[i][c]for i in face)-min(vertices[i][c]for i in face));axes=[c for c in range(3)if c!=axis];lo=[min(vertices[i][c]for i in face)for c in axes];hi=[max(vertices[i][c]for i in face)for c in axes];indices=[]
 for i in face:
  u=(vertices[i][axes[0]]-lo[0])/(hi[0]-lo[0])-.5;v=(vertices[i][axes[1]]-lo[1])/(hi[1]-lo[1])-.5;t=.2
  # Deliberately authored orientation, within a unique cell per polygon.
  uv.append(((n%4+.5+.65*(u*math.cos(t)-v*math.sin(t)))/4,(n//4+.5+.65*(u*math.sin(t)+v*math.cos(t)))/2));indices.append(len(uv))
 uv_faces.append(indices)
(a.output/'mechanical-uv.obj').write_text(header+''.join('vt %.9g %.9g\n'%point for point in uv)+''.join('f '+' '.join(f'{v+1}/{t}'for v,t in zip(face,indices))+'\n'for face,indices in zip(faces,uv_faces)))
env=dict(os.environ,LANG='sv_SE.UTF-8',LC_ALL='sv_SE.UTF-8',DISPLAY=private_display(),XDG_RUNTIME_DIR='/run/user/1000',XDG_CONFIG_HOME=str(a.output/'config'),XDG_DATA_HOME=str(a.output/'data'),XDG_CACHE_HOME=str((a.cache or a.output/'cache').resolve()),BTS_HARD_UNWRAP_DEMO=str(a.output))
if not a.check_only:
 with (a.output/'run.log').open('w')as log:
  process=subprocess.Popen(['bash',str(root/'scripts/host/run-host.sh'),'--workspace','--library-dir',str(a.output/'shelf'),'--recovery-dir',str(a.output/'recovery')],env=env,stdout=log,stderr=subprocess.STDOUT)
  try:code=process.wait(timeout=300)
  except subprocess.TimeoutExpired:process.terminate();process.wait(timeout=20);raise
 assert code==0,(code,(a.output/'run.log').read_text()[-6000:])
d=json.loads((a.output/'unwrap.json').read_text());c={s['stage']:s for s in d['captures']};assert len(c)==8
if not a.check_only:assert d['pid']==process.pid
def triangles(stage):return c[stage]['triangles']
def geometry(stage):return [t['positions']for t in triangles(stage)]
def uv_data(stage):return [t['uv']for t in triangles(stage)]
def quality(stage):
 aligned=edges=0;areas=0;ratios=[]
 for t in triangles(stage):
  uv=t['uv'];positions=t['positions'];ab=[uv[1][i]-uv[0][i]for i in range(2)];ac=[uv[2][i]-uv[0][i]for i in range(2)];area=abs(ab[0]*ac[1]-ab[1]*ac[0])*.5;assert area>1e-12;areas+=area
  for n in range(3):
   assert all(-1e-6<=v<=1.000001 for v in uv[n]);m=(n+1)%3;delta=[uv[m][i]-uv[n][i]for i in range(2)];distance=math.dist(positions[n],positions[m]);assert distance>0;ratios.append(math.hypot(*delta)/distance)
   if t['boundary']&(1<<n):edges+=1;aligned+=min(abs(delta[0]),abs(delta[1]))<1e-5
 assert .05<areas<=1.00001
 # Uniform texel density over orthographic projections of axis-aligned planar faces.
 distortion=max(ratios)/min(ratios)-1
 # Check UV interiors do not overlap, ignoring exact shared boundaries.
 overlap=0
 for y in range(100):
  for x in range(100):
   point=((x+.314159)/100,(y+.271828)/100);count=0
   for t in triangles(stage):
    aa,bb,cc=t['uv'];det=(bb[0]-aa[0])*(cc[1]-aa[1])-(bb[1]-aa[1])*(cc[0]-aa[0]);u=((point[0]-aa[0])*(cc[1]-aa[1])-(point[1]-aa[1])*(cc[0]-aa[0]))/det;v=((bb[0]-aa[0])*(point[1]-aa[1])-(bb[1]-aa[1])*(point[0]-aa[0]))/det;count+=u>1e-6 and v>1e-6 and u+v<1-1e-6
   overlap+=count>1
 assert not overlap,(stage,overlap)
 return {'axis_aligned_boundary_fraction':aligned/edges,'uv_area':areas,'scale_distortion':distortion,'overlap_samples':overlap}
def contour_visibility(stage):
 state=c[stage];view=state['view'];pane=next(p['rect']for p in view['panes']if p['view']=='2D');x,y,w,h=pane;center=view['uv_center'];scale=h/view['uv_vertical_extent'];image=Image.open(a.output/(stage+'-viewport.png')).convert('RGB');edges={}
 for t in triangles(stage):
  for n in range(3):
   if t['boundary']&(1<<n):
    edge=tuple(sorted((tuple(t['uv'][n]),tuple(t['uv'][(n+1)%3]))));edges[edge]=edges.get(edge,0)+1
 visible=total=0
 for (aa,bb),count in edges.items():
  if count!=1:continue
  for fraction in [.2,.35,.5,.65,.8]:
   u=aa[0]+fraction*(bb[0]-aa[0]);v=aa[1]+fraction*(bb[1]-aa[1])
   if not(.03<u<.97 and .03<v<.97):continue
   px=round(x+w/2+(u-center[0])*scale);py=round(y+h/2-(v-center[1])*scale)
   if not(2<px<image.width-2 and 2<py<image.height-2):continue
   total+=1;visible+=min(max(image.getpixel((px+dx,py+dy)))for dx in range(-2,3)for dy in range(-2,3))<140
 assert total>20,total
 return {'visible_samples':visible,'expected_samples':total,'fraction':visible/total}
overlays=contour_visibility('hard');assert overlays['fraction']>.8,('UV contours missing from actual Qt framebuffer',overlays)
standard=quality('standard');hard=quality('hard');assert hard['axis_aligned_boundary_fraction']>.999 and hard['scale_distortion']<.01,hard
# This planar L-profile is also aligned by legacy packing; do not assume it is worse.
assert hard['axis_aligned_boundary_fraction']>=standard['axis_aligned_boundary_fraction']-1e-6,(standard,hard)
assert geometry('hard')==geometry('standard') and geometry('recomputed')==geometry('hard')
assert uv_data('hard')!=uv_data('standard')
assert uv_data('authored')==uv_data('preserved') and uv_data('recomputed')!=uv_data('preserved')
assert uv_data('hard')==uv_data('recomputed')
assert c['saved']['document']==c['opened']['document'] and triangles('saved')==triangles('opened')
assert not c['cancelled-options']['document']['auto_unwrap']['hard_surface']
for stage in ['hard','saved','opened','preserved','recomputed']:assert c[stage]['document']['auto_unwrap']['hard_surface']
assert c['recomputed']['document']['auto_unwrap']['seams']==c['recomputed']['document']['auto_unwrap']['islands']==c['recomputed']['document']['auto_unwrap']['packing']==1
for stage in c:
 assert not c[stage]['shader_failures'];image=Image.open(a.output/(stage+'-viewport.png')).convert('RGB');assert not any(r>235 and g<20 and b>235 for r,g,b in image.getdata()),stage
assert 'BTS_SHUTDOWN clean' in (a.output/'run.log').read_text()
proof={'pid':d['pid'],'locale':'sv_SE.UTF-8','stages':list(c),'standard':standard,'hard_surface':hard,'uv_contour_visibility':overlays,'source_geometry_unchanged':True,'authored_uv_preserved':True,'recompute_dependencies':True,'hard_projection_reproducible':True,'cancel_options_retained_default':True,'packed_open_exact':True,'magenta_pixels':0}
(a.output/'verified.json').write_text(json.dumps(proof,indent=2)+'\n');print(json.dumps(proof,indent=2))
