#!/usr/bin/env python3
"""BC: generated 5.12M triangle OBJ -> real native New project -> UV/EEVEE, no windows."""
import argparse,json,os,re,subprocess,time
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);p.add_argument('--mesh',type=Path);p.add_argument('--generate-only',action='store_true');a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True)
mesh=a.mesh or a.output/'grid-5120000.obj'
if not mesh.exists():
 t=time.monotonic();n=1600
 mesh.with_suffix('.mtl').write_text('newmtl BudgetSurface\nKd 0.8 0.3 0.1\nPr 0.5\n')
 with mesh.open('w',buffering=1024*1024) as f:
  f.write(f'mtllib {mesh.with_suffix(".mtl").name}\no BudgetGrid\nusemtl BudgetSurface\n')
  for y in range(n+1):
   for x in range(n+1):f.write(f'v {x/n:.8f} {y/n:.8f} 0\n')
  for y in range(n+1):
   for x in range(n+1):f.write(f'vt {x/n:.8f} {y/n:.8f}\n')
  for y in range(n):
   row=y*(n+1)+1
   for x in range(n):
    q=row+x;r=q+n+1
    f.write(f'f {q}/{q} {q+1}/{q+1} {r+1}/{r+1}\nf {q}/{q} {r+1}/{r+1} {r}/{r}\n')
 (a.output/'generation.json').write_text(json.dumps({'triangles':2*n*n,'vertices':(n+1)**2,'seconds':time.monotonic()-t,'source_bytes':mesh.stat().st_size},indent=2))
if a.generate_only:print(mesh);raise SystemExit()
assert os.environ.get('QT_QPA_PLATFORM')=='offscreen','Must run in private offscreen environment'
env=dict(os.environ,BTS_LARGE_MESH_TEST=str(mesh.resolve()),BTS_HEADLESS_WORKSPACE='1')
root=Path(os.environ.get('BTS_ROOT',str(Path.home()/'bts')))
repo=Path(__file__).resolve().parents[2]
command=['bash',str(repo/'scripts/host/run-host.sh'),'--headless-viewport-test',str(a.output),'--no-splash']
with (a.output/'host.log').open('w') as log:subprocess.run(command,env=env,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=900)
proof=json.loads((a.output/'bc-large-mesh.json').read_text());log=(a.output/'host.log').read_text();renderer=re.search(r'BTS_GPU_READY.*renderer=(.*?) qt=',log);assert renderer and 'NVIDIA' in renderer[1], 'Large mesh acceptance requires the RTX renderer';proof['renderer']=renderer[1];(a.output/'bc-large-mesh.json').write_text(json.dumps(proof,indent=2)+'\n');assert proof['triangles']>=5000000 and proof['uv_triangles']>=5000000 and proof['visible_windows']==0
print(json.dumps(proof,indent=2))
