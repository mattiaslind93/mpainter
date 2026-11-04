"""Private GUI object/UV-island bucket, channels, Ctrl/masks, Undo and packed Open."""
import argparse,json,os,subprocess,struct
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);p.add_argument('--resolution',type=int,default=4096);p.add_argument('--subdivisions',type=int,default=1);a=p.parse_args();out=a.output.resolve();out.mkdir(parents=True,exist_ok=True);root=Path(__file__).resolve().parents[2]
(out/'bucket.mtl').write_text('newmtl FixtureSurface\nKd 1 1 1\nPr 0.3\nPm 0\nillum 2\n')
mesh=out/'bucket.obj';mesh.write_text('''mtllib bucket.mtl
o JoinedQuads
v -1 -0.6 0
v -0.5 -0.6 0
v -0.5 0.6 0
v -1 0.6 0
v 0 -0.6 0
v 0 0.6 0
vt 0.1 0.1
vt 0.4 0.1
vt 0.4 0.6
vt 0.1 0.6
vt 1.1 0.1
vt 1.4 0.1
vt 1.4 0.6
vt 1.1 0.6
usemtl FixtureSurface
f 1/1 2/2 3/3 4/4
f 2/5 5/6 6/7 3/8
o OtherObject
v 0.2 -0.6 0
v 1.2 -0.6 0
v 1.2 0.6 0
v 0.2 0.6 0
vt 0.6 0.6
vt 0.9 0.6
vt 0.9 0.9
vt 0.6 0.9
usemtl FixtureSurface
f 7/9 8/10 9/11 10/12
''')
# Same objects/islands and probe points with a denser native topology.
if a.subdivisions>1:
 n=a.subdivisions;vertices=[];uvs=[];groups=[];indices={}
 for name,x0,x1,u0,u1,v0,v1 in [('JoinedQuads',-1,-.5,.1,.4,.1,.6),('JoinedQuads',-.5,0,1.1,1.4,.1,.6),('OtherObject',.2,1.2,.6,.9,.6,.9)]:
  grid={}
  for j in range(n+1):
   for i in range(n+1):
    xyz=(round(x0+(x1-x0)*i/n,9),round(-.6+1.2*j/n,9),0)
    key=(name,xyz)
    if key not in indices:vertices.append(xyz);indices[key]=len(vertices)
    uvs.append((u0+(u1-u0)*i/n,v0+(v1-v0)*j/n));grid[i,j]=(indices[key],len(uvs))
  faces=[]
  for j in range(n):
   for i in range(n):faces.append([grid[i,j],grid[i+1,j],grid[i+1,j+1],grid[i,j+1]])
  if groups and groups[-1][0]==name:groups[-1][1].extend(faces)
  else:groups.append((name,faces))
 lines=['mtllib bucket.mtl']+['v %g %g %g'%v for v in vertices]+['vt %.9g %.9g'%uv for uv in uvs]
 for name,faces in groups:
  lines.extend(['o '+name,'usemtl FixtureSurface']);lines.extend('f '+' '.join('%d/%d'%v for v in face)for face in faces)
 mesh.write_text('\n'.join(lines)+'\n')
env=dict(os.environ,QT_QPA_PLATFORM='offscreen',BTS_HEADLESS_WORKSPACE='1',BTS_POLYGON_BUCKET_DEMO='1',BTS_PERFORMANCE_PROFILE='1',LANG='sv_SE.UTF-8',LC_ALL='sv_SE.UTF-8')
for key,folder in [('XDG_CONFIG_HOME','config'),('XDG_DATA_HOME','data'),('XDG_CACHE_HOME','cache'),('TMPDIR','tmp'),('BTS_RECOVERY_DIR','recovery'),('BTS_RESOURCE_CACHE','resources')]:
 (out/folder).mkdir(exist_ok=True);env[key]=str(out/folder)
cmd=['bash',str(root/'scripts/host/run-host.sh'),'--workspace','--gui-fix-demo',str(out),'--gui-fix-mesh',str(mesh),'--gui-fix-resolution',str(a.resolution),'--library-dir',str(out/'library'),'--autosave-seconds','3600']
with(out/'run.log').open('w')as log:subprocess.run(cmd,env=env,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=360)
d=json.loads((out/'polygon-bucket.json').read_text());d['fixture_triangles']=6*a.subdivisions**2;captures={c['stage']:c for c in d['captures']};red=[1,0,0,1];blue=[0,0,1,1];empty=[0,0,0,0]
expected={'initial':[empty,empty],'object-red':[red,red],'undo-object':[empty,empty],'redo-object':[red,red],'uv-red':[red,empty],'uv-blue-1002':[red,blue],'black-mask':[empty,empty],'black-white-ink':[red,empty],'black-ctrl':[empty,empty],'undo-mask':[red,empty],'redo-mask':[empty,empty],'white-default':[red,blue],'white-black-ink':[empty,blue],'white-ctrl':[red,blue],'reopened':[red,blue]}
for name,colors in expected.items():
 c=captures[name];assert c['displacement_default_off'] and c['metallic_pages']==0,(name,c)
 for i,color in enumerate(colors+[empty,empty]):
  actual=c['samples'][i][0]or empty;assert len(actual)==4 and max(abs(x-y)for x,y in zip(actual,color))<1e-5,(name,i,actual,color)
  metal=c['samples'][i][1]or empty;assert all(x==0 for x in metal),(name,i,metal)
  for slot,value in [(2,struct.unpack('e',struct.pack('e',59/255))[0]),(3,struct.unpack('e',struct.pack('e',.01))[0])]:
   expected_scalar=([value]*3+[1])if color[3]else empty
   assert max(abs(x-y)for x,y in zip(c['samples'][i][slot]or empty,expected_scalar))<1e-5,(name,i,slot,c['samples'][i][slot],expected_scalar)
 assert c['native_object_count']==2 and c['document']['texture_sets'][0]['tiles']==[1001,1002],name
assert captures['object-red']['undo_count']==captures['initial']['undo_count']+1
assert captures['initial']['cpu_materializations']==captures['white-ctrl']['cpu_materializations'],'CPU raster under interactive bucket'
assert captures['reopened']['samples']==captures['white-ctrl']['samples'],'Packed roundtrip changed GPU pixels'
assert all(c['handler_ms']<50 for c in d['calls']),d['calls'];assert d['max_interaction_ping_ms']<100,d['max_interaction_ping_ms']
log=(out/'run.log').read_text();assert 'BTS_SHUTDOWN clean'in log and 'BTS_BRUSH_CPU_FALLBACK'not in log and 'BTS_FINALIZE_FALLBACK'not in log
assert 'BTS_GPU_POLYGON_RASTER' in log and 'BTS_GPU_POLYGON_UNDO' in log,'Missing batched GPU raster/dirty Undo'
assert not any(r['phase']=='gpu_authoring_page' and r['flow'].startswith('polygon_bucket_')for r in d['phases']),'Interactive bucket regressed to per-page replay'
assert (out/'polygon.blend').is_file();d['verified']=True;(out/'verified.json').write_text(json.dumps(d,indent=2)+'\n');print('Polygon bucket: 3D object, UV islands/1002, disabled channels, Ctrl, black/white masks, Undo and packed Open PASS')
