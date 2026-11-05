"""Exercise native File/Export Mesh, preserved polygons, triangulation and generated UVs."""
from parity_test_environment import private_display
import argparse,collections,json,math,os,re,subprocess
from pathlib import Path
from PIL import Image,ImageChops,ImageStat
p=argparse.ArgumentParser();p.add_argument('--output',required=True,type=Path);p.add_argument('--cache',type=Path);p.add_argument('--check-only',action='store_true');p.add_argument('--no-uv',action='store_true');a=p.parse_args();a.output=a.output.resolve();a.output.mkdir(parents=True,exist_ok=True);root=Path(__file__).resolve().parents[2]
mesh=a.output/'MeshExport.obj';source=(root/'native/tests/fixtures/single_tile.obj').read_text().replace('mtllib single_tile.mtl','mtllib MeshExport.mtl');lines=[];index=0
for line in source.splitlines():
 if line.startswith('usemtl '):line='usemtl '+['SphereSurface','CubeSurface'][index];index+=1
 if a.no_uv:
  if line.startswith('vt '):continue
  if line.startswith('f '):line='f '+' '.join(t.split('/')[0]+'//'+t.split('/')[-1] for t in line.split()[1:])
 lines.append(line)
mesh.write_text('\n'.join(lines)+'\n');mtl=(root/'native/tests/fixtures/single_tile.mtl').read_text();(a.output/'MeshExport.mtl').write_text(mtl.replace('FixtureSurface','SphereSurface')+'\n'+mtl.replace('FixtureSurface','CubeSurface'))
env=dict(os.environ,LANG='sv_SE.UTF-8',LC_ALL='sv_SE.UTF-8',DISPLAY=private_display(),XDG_RUNTIME_DIR='/run/user/1000',XDG_CONFIG_HOME=str(a.output/'config'),XDG_CACHE_HOME=str((a.cache or a.output/'cache').resolve()),BTS_MESH_EXPORT_DEMO='1',BTS_SKEW_DEMO='1')
if a.no_uv:env['BTS_MESH_EXPORT_AUTO_UV']='1'
if not a.check_only:
 with (a.output/'run.log').open('w')as log:
  proc=subprocess.Popen(['bash',str(root/'scripts/host/run-host.sh'),'--workspace','--gui-fix-demo',str(a.output),'--gui-fix-mesh',str(mesh),'--gui-fix-resolution','128','--library-dir',str(a.output/'shelf'),'--recovery-dir',str(a.output/'recovery')],env=env,stdout=log,stderr=subprocess.STDOUT)
  try:code=proc.wait(timeout=300)
  except subprocess.TimeoutExpired:proc.terminate();proc.wait(timeout=20);raise
 assert code==0,(code,(a.output/'run.log').read_text()[-3000:])
def read(path):
 d=dict(v=[],vt=[],vn=[],faces=[],material=[],mtl=[]);material=''
 for line in path.read_text().splitlines():
  parts=line.split()
  if not parts:continue
  tag,*values=parts
  if tag in ['v','vt','vn']:d[tag].append(tuple(map(float,values)));assert all(math.isfinite(x)for x in d[tag][-1])
  elif tag=='usemtl':material=' '.join(values)
  elif tag=='mtllib':d['mtl'].append(path.parent/' '.join(values))
  elif tag=='f':
   d['faces'].append([tuple(int(x)if x else 0 for x in token.split('/'))for token in values]);d['material'].append(material)
 for face in d['faces']:
  for c in face:
   for i,field in enumerate(['v','vt','vn']):
    if len(c)>i and c[i]:assert 0<c[i]<=len(d[field]),(path,field,c)
 return d
original=read(mesh);triangles=read(a.output/'triangles.obj');polygons=read(a.output/'polygons.obj')
quant=lambda p:tuple(round(x,5)for x in p)
assert collections.Counter(map(quant,original['v']))==collections.Counter(map(quant,polygons['v']))==collections.Counter(map(quant,triangles['v']))
assert collections.Counter(map(len,original['faces']))==collections.Counter(map(len,polygons['faces']))
assert len(triangles['faces'])==sum(len(f)-2 for f in original['faces'])and all(len(f)==3 for f in triangles['faces'])
for d in [polygons,triangles]:
 assert set(d['material'])==set(original['material'])and all(m.exists()for m in d['mtl'])
 assert d['vt']and d['vn']and all(len(c)==3 and c[1]and c[2]for f in d['faces']for c in f)
def corners(d):return {(d['material'][i],quant(d['v'][c[0]-1]),quant(d['vt'][c[1]-1]))for i,f in enumerate(d['faces'])for c in f}
assert corners(polygons)==corners(triangles)
if not a.no_uv:assert corners(original)==corners(polygons)
proof=json.loads((a.output/'mesh-export.json').read_text());captures={s['stage']:s for s in proof['captures']};base=captures['before']
for name,s in captures.items():
 assert s['document']==base['document']and s['source_obj_hash']==base['source_obj_hash']and s['undo_count']==base['undo_count'],name
 assert s['decimal']=='.'and not s['shader_failures']
 for key in ['scene_center','scene_distance','scene_yaw','scene_pitch','scene_projection','uv_center','uv_vertical_extent']:assert s['view'][key]==base['view'][key],(name,key)
 before=Image.open(a.output/'before-viewport.png').convert('RGB');after=Image.open(a.output/(name+'-viewport.png')).convert('RGB');assert sum(ImageStat.Stat(ImageChops.difference(before,after)).mean)/765<.001,name
assert len(proof['exports'])==2 and all(e['status']=='Mesh export successfully finished'and e['source']['source_geometry']for e in proof['exports'])
log=(a.output/'run.log').read_text();assert f'BTS_SHUTDOWN clean pid={proof["pid"]}'in log and not re.search(r'BTS_DRAW_FAILED|Shader compilation failed',log,re.I)
result=dict(pid=proof['pid'],locale='sv_SE.UTF-8',native_blender_obj_export=True,source_vertices=len(original['v']),source_faces=len(original['faces']),exported_triangles=len(triangles['faces']),polygon_topology_preserved=True,uv_corner_values_preserved=True,generated_uvs=a.no_uv,material_names=sorted(set(polygons['material'])),source_document_camera_undo_unchanged=True,cancel_unchanged=True,clean_shutdown=True)
(a.output/'verified.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
