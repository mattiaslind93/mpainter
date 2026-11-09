# SPDX-License-Identifier: GPL-3.0-or-later
"""Real GUI export of signed displacement and both native preview subdivisions."""
from parity_test_environment import private_display
import argparse,json,math,os,re,subprocess
from pathlib import Path
from PIL import Image,ImageChops,ImageStat
p=argparse.ArgumentParser();p.add_argument('--output',required=True,type=Path);p.add_argument('--cache',type=Path);p.add_argument('--check-only',action='store_true');a=p.parse_args();a.output=a.output.resolve();a.output.mkdir(parents=True,exist_ok=True);root=Path(__file__).resolve().parents[2]
vertices=[(-1,-1,-1),(1,-1,-1),(1,1,-1),(-1,1,-1),(-1,-1,1),(1,-1,1),(1,1,1),(-1,1,1)];faces=[(0,3,2,1),(4,5,6,7),(0,1,5,4),(1,2,6,5),(2,3,7,6),(3,0,4,7)];mesh=a.output/'ExportCube.obj';lines=['mtllib ExportCube.mtl','o ExportCube']+['v '+' '.join(map(str,v))for v in vertices]+['vn '+' '.join(str(x/math.sqrt(3))for x in v)for v in vertices]
for face in faces:
 for uv in [(.1,.1),(.9,.1),(.9,.9),(.1,.9)]:lines.append('vt '+' '.join(map(str,uv)))
lines+=['usemtl FixtureSurface','s 1']
for i,face in enumerate(faces):lines.append('f '+' '.join(f'{v+1}/{i*4+c+1}/{v+1}'for c,v in enumerate(face)))
mesh.write_text('\n'.join(lines)+'\n');(a.output/'ExportCube.mtl').write_text('newmtl FixtureSurface\nKd 1 1 1\nPr 0.38\nPm 0\n')
env=dict(os.environ,LANG='sv_SE.UTF-8',LC_ALL='sv_SE.UTF-8',DISPLAY=private_display(),XDG_RUNTIME_DIR='/run/user/1000',XDG_CONFIG_HOME=str(a.output/'config'),XDG_CACHE_HOME=str((a.cache or a.output/'cache').resolve()),BTS_MESH_EXPORT_DEMO='1',BTS_MESH_EXPORT_DISPLACED='1',BTS_SKEW_DEMO='1',BTS_DISPLACEMENT_DEMO='1')
if not a.check_only:
 with (a.output/'run.log').open('w')as log:
  proc=subprocess.Popen(['bash',str(root/'scripts/host/run-host.sh'),'--workspace','--gui-fix-demo',str(a.output),'--gui-fix-mesh',str(mesh),'--gui-fix-resolution','512','--library-dir',str(a.output/'shelf'),'--recovery-dir',str(a.output/'recovery')],env=env,stdout=log,stderr=subprocess.STDOUT)
  try:code=proc.wait(timeout=300)
  except subprocess.TimeoutExpired:proc.terminate();proc.wait(timeout=20);raise
 assert code==0,(code,(a.output/'run.log').read_text()[-4000:])
def obj(name):
 d=dict(v=[],vt=[],vn=[],f=[],material=[]);material=''
 for line in (a.output/(name+'.obj')).read_text().splitlines():
  parts=line.split()
  if not parts:continue
  key,*values=parts
  if key in ['v','vt','vn']:d[key].append(tuple(map(float,values)));assert all(math.isfinite(x)for x in d[key][-1])
  elif key=='usemtl':material=' '.join(values)
  elif key=='f':d['f'].append([tuple(map(int,t.split('/')))for t in values]);d['material'].append(material)
 for f in d['f']:
  for corner in f:
   assert len(corner)==3
   for index,key in zip(corner,['v','vt','vn']):assert 0<index<=len(d[key])
 return d
names=['base-before','level0','normals-retained','negative','uniform','catmull','texture-recomputed','texture-retained','base-disabled'];data={n:obj(n)for n in names};base=data['base-before'];assert base==data['base-disabled'];assert len(base['v'])==8 and len(base['f'])==6
for name,height in [('level0',.8),('negative',.8)]:
 mid=.5 if name=='level0'else 1;factor=1+(height-mid)*.25/math.sqrt(3);out=data[name];assert len(out['v'])==8 and len(out['f'])==12
 for v in out['v']:assert max(abs(abs(x)-factor)for x in v)<3e-6,(name,v,factor)
assert data['level0']['v']==data['normals-retained']['v']
assert [[c[:2]for c in f]for f in data['level0']['f']]==[[c[:2]for c in f]for f in data['normals-retained']['f']]
for name in ['uniform','catmull']:
 assert len(data[name]['v'])>8 and len(data[name]['f'])>12
 assert all(len(f)==3 for f in data[name]['f'])
assert len(data['uniform']['v'])==len(data['catmull']['v'])
extent=lambda d:max(abs(x)for v in d['v']for x in v)
assert extent(data['catmull'])<extent(data['uniform'])-.1,(extent(data['catmull']),extent(data['uniform']))
a1,b1=data['texture-recomputed'],data['texture-retained'];assert a1['v']==b1['v']and a1['vt']==b1['vt']
assert len(a1['v'])>8
for face in a1['f']:
 for vertex,uv,normal in face:
  u=a1['vt'][uv-1][0];height=(u*256-.5)/255;factor=1+(height-.5)*.25/math.sqrt(3)
  assert max(abs(abs(x)-factor)for x in a1['v'][vertex-1])<.002,(u,a1['v'][vertex-1],factor)
assert [tuple(c[:2]for c in f)for f in a1['f']]==[tuple(c[:2]for c in f)for f in b1['f']]
normal_difference=max(abs(a-b)for fa,fb in zip(a1['f'],b1['f'])for ca,cb in zip(fa,fb)for a,b in zip(a1['vn'][ca[2]-1],b1['vn'][cb[2]-1]));assert normal_difference>.01,normal_difference
proof=json.loads((a.output/'mesh-export.json').read_text());c={s['stage']:s for s in proof['captures']}
pairs=[('base-before','base-after'),('level0-before','level0-after'),('level0-after','normals-retained-after'),('negative-before','negative-after'),('uniform-before','uniform-after'),('catmull-before','catmull-after'),('texture-before','texture-after'),('texture-after','texture-retained-after'),('disabled-before','disabled-after')]
for before,after in pairs:
 x,y=c[before],c[after];assert x['document']==y['document']and x['undo_count']==y['undo_count'],(before,after)
 for key in ['scene_center','scene_distance','scene_yaw','scene_pitch','scene_projection','uv_center','uv_vertical_extent']:assert x['view'][key]==y['view'][key],(before,key)
 im1=Image.open(a.output/(before+'-viewport.png')).convert('RGB');im2=Image.open(a.output/(after+'-viewport.png')).convert('RGB');assert sum(ImageStat.Stat(ImageChops.difference(im1,im2)).mean)/765<.001,(before,after)
for s in c.values():assert s['source_obj_hash']==c['base-before']['source_obj_hash']and s['decimal']=='.'and not s['shader_failures']
pane=next(p for p in c['catmull-before']['view']['panes']if p['view']=='2D');x,y,w,h=pane['rect'];im=Image.open(a.output/'catmull-before-viewport.png').convert('RGB');flat=[v for u in [.3,.5,.7]for t in [.3,.5,.7]for v in im.getpixel((int(x+w*u),int(y+h*t)))];assert max(flat)-min(flat)<=1
assert len(proof['exports'])==9
for e in proof['exports']:assert e['source']['source_geometry']!=e['displaced']and e['source']['recompute_normals']==e['recompute']and e['status']=='Mesh export successfully finished'
log=(a.output/'run.log').read_text();assert f'BTS_SHUTDOWN clean pid={proof["pid"]}'in log and not re.search(r'BTS_DRAW_FAILED|Shader compilation failed',log,re.I)
result=dict(pid=proof['pid'],locale='sv_SE.UTF-8',native_gui_export=True,signed_uniform_displacement_exact=True,subdivision_vertices={n:len(data[n]['v'])for n in names},catmull_vs_uniform_distinct=True,normal_recompute_difference=normal_difference,base_output_identical_after_preview=True,source_uv_camera_layers_undo_preserved=True,texture_seams_preserved=True,unlit_uv_uniform_max_delta=max(flat)-min(flat),clean_shutdown=True)
(a.output/'verified.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
