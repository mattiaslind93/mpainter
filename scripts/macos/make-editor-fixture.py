#!/usr/bin/env python3
"""Small deterministic, two-material UV mesh for the existing editor demos."""
from pathlib import Path
import math
import sys
out=Path(sys.argv[1]);out.mkdir(parents=True,exist_ok=False)
vertices=[];uvs=[];lines=['mtllib two_materials.mtl']
def face(points,coords):
 start=len(vertices)+1
 for p,uv in zip(points,coords):vertices.append(p);uvs.append(uv)
 return 'f '+' '.join(f'{i}/{i}' for i in range(start,len(vertices)+1))
lines+=['o Cube','usemtl CubeSurface']
corners=[(-1,-1,-1),(1,-1,-1),(1,1,-1),(-1,1,-1),(-1,-1,1),(1,-1,1),(1,1,1),(-1,1,1)]
for n,ids in enumerate([(0,3,2,1),(4,5,6,7),(0,1,5,4),(1,2,6,5),(2,3,7,6),(3,0,4,7)]):
 x,y=n%3,n//3
 coords=[((x+.05)/3,(y+.05)/2),((x+.95)/3,(y+.05)/2),((x+.95)/3,(y+.95)/2),((x+.05)/3,(y+.95)/2)]
 lines.append(face([(corners[i][0]*.8+1.25,corners[i][1]*.8,corners[i][2]*.8-.15) for i in ids],coords))
lines+=['o Sphere','usemtl SphereSurface','s 1']
for y in range(12):
 for x in range(24):
  coords=[(x/24,y/12),((x+1)/24,y/12),((x+1)/24,(y+1)/12),(x/24,(y+1)/12)]
  points=[(-1.15+math.sin(v*math.pi)*math.cos(u*math.tau),math.sin(v*math.pi)*math.sin(u*math.tau),-math.cos(v*math.pi)) for u,v in coords]
  lines.append(face(points,coords))
(out/'two_materials.obj').write_text('\n'.join(['# mPainter Metal editor fixture']+['v '+' '.join(map(str,p)) for p in vertices]+['vt '+' '.join(map(str,p)) for p in uvs]+lines)+'\n')
(out/'two_materials.mtl').write_text('newmtl CubeSurface\nKd 0.2 0.4 0.7\nNs 32\n\nnewmtl SphereSurface\nKd 0.6 0.3 0.1\nNs 32\n')
