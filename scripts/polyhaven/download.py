#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Review/fetch public Poly Haven API metadata and checksum-pinned source maps, serially."""
import argparse, hashlib, json, time, urllib.request, urllib.error
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--mapping',type=Path,required=True);p.add_argument('--work',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
a.work.mkdir(parents=True,exist_ok=True);a.output.mkdir(parents=True,exist_ok=True)
def get(url,path):
 if not path.exists():
  for attempt in range(4):
   try:
    with urllib.request.urlopen(urllib.request.Request(url,headers={'User-Agent':'mPainter CC0 starter library preparation'}),timeout=90) as r: data=r.read()
    temp=path.with_suffix(path.suffix+'.part');temp.write_bytes(data);temp.replace(path);break
   except urllib.error.HTTPError as e:
    if e.code==404:return None
    if attempt==3:raise
    time.sleep(2+attempt)
   except (OSError,TimeoutError):
    if attempt==3:raise
    time.sleep(2+attempt)
 return path

def api(endpoint,filename):
 f=get('https://api.polyhaven.com/'+endpoint,a.work/filename)
 return json.loads(f.read_text()) if f else None
assets=api('assets?t=textures','assets.json')
# Neutral names only. These judgments use public title/category/tag metadata.
changes={
 'Brushed Iron':('procedural','Fine directional brushed iron; distinct from smooth Polished Iron.'),
 'Grainy Plastic':('procedural','Sage green granular polymer, distinct from blue Powder Coat and beige materials.'),
 'Calf Hide':('leather_white','Warm tan smooth hide, distinct from grey Dusty Concrete.'),
 'Coated Metal':('blue_metal_plate','Painted/coated sheet instead of bare patterned plate.'),
 'Clean Concrete':('concrete','Bare concrete panels; avoids moss in original floor texture.'),
 'Plain Concrete':('rough_concrete','Plain cement instead of stained painted wall.'),
 'Denim':('denim_fabric_03','Even twill weave without the large folds of the original.'),
 'Suit Tweed':('poly_wool_herringbone','Wool blend herringbone instead of picnic/towel checks.'),
 'Galvanized Steel':('corrugated_iron_03','Galvanized steel sheeting; corrugation retained as a surface approximation.'),
 'Raw Iron':('procedural','Blue-black mill scale with broad worn exposed iron patches.'),
 'Rough Iron':('procedural','Warm dark cast iron with coarse granular relief and high roughness.'),
 'Worn Leather':('fabric_leather_01','Faded cool grey-brown worn hide, distinct from reddish Soft Leather and dark Coarse Leather.'),
 'Fine Rust':('rusty_metal_04','Fine red-brown surface corrosion; visually checked diffuse, replacing greenish tarnished sheet.'),
 'Painted Steel':('painted_metal_shutter','Painted sheet; corrugated shutter is an approximation.'),
 'Rough Steel':('procedural','Cool steel with directional abrasion and varying satin roughness.'),
 'Cherry Wood':('cherry_veneer','Actual cherry wood grain instead of weathered generic planks.'),
 'Rough Wood':('rough_wood','Rough natural timber instead of furniture tabletop.'),
 'Walnut Wood':('walnut_veneer','Oiled dark walnut tint preserves original veneer grain; distinct from pale Cherry Wood.'),
 'Wool':('polar_fleece','Plain cream napped fleece: wool-like surface approximation, without the checker weave of wool_boucle.'),
 'Lichen Rock':('lichen_rock','Actual lichen-covered rock instead of generic moss.'),
 'Stone Wall':('rustic_stone_wall','Stone masonry wall instead of natural rock face.'),
}
rows=[]
for old in json.loads(a.mapping.read_text()):
 name=old.get('mpainter_name',old.get('name'));original=old.get('original_slug',old['polyhaven_slug']);slug,note=changes.get(name,(original,'Original procedural shader.' if original=='procedural' else 'Retained: API texture type and surface tags match the intended material.'))
 # Audit original candidates too, including deleted/nonexistent automatic matches.
 for candidate in dict.fromkeys([original,slug]):
  if candidate!='procedural':
   api('info/'+candidate,candidate+'-info.json');api('files/'+candidate,candidate+'-files.json')
 if slug!='procedural':assert slug in assets,(name,slug)
 rows.append(dict(name=name,slug=name.lower().replace(' ','-'),polyhaven_slug=slug,original_slug=original,url='procedural' if slug=='procedural' else 'https://polyhaven.com/a/'+slug,review=note,source_blend=name.lower().replace(' ','-')+'.blend'))

def maps(row,res):
 if row['polyhaven_slug']=='procedural':return {}
 files=api('files/'+row['polyhaven_slug'],row['polyhaven_slug']+'-files.json');out={}
 for channel in ['Diffuse','nor_gl','Rough','Metal','AO','Displacement']:
  source_channel='coll1' if channel=='Diffuse' and 'Diffuse' not in files and 'coll1' in files else channel
  formats=files.get(source_channel,{}).get(res,{})
  if not formats:continue
  preferred=['png','exr','jpg'] if channel in ('nor_gl','Displacement') else ['jpg','png','exr']
  kind=next(k for k in preferred if k in formats);out[channel]=dict(formats[kind],format=kind)
 assert {'Diffuse','nor_gl','Rough'}<=out.keys(),row['name']
 return out
size2=sum(v['size'] for r in rows for v in maps(r,'2k').values())
res='1k' if size2>400_000_000 else '2k'
for row in rows:
 row['resolution']=None if row['polyhaven_slug']=='procedural' else res;row['maps']=maps(row,res)
 for channel,record in row['maps'].items():
  filename=record['url'].rsplit('/',1)[1];dest=a.output/filename
  get(record['url'],dest)
  assert dest.stat().st_size==record['size'],filename
  assert hashlib.md5(dest.read_bytes()).hexdigest()==record['md5'],filename
  record['sha256']=hashlib.sha256(dest.read_bytes()).hexdigest();record['file']=filename
 print(row['name'],row['polyhaven_slug'],flush=True)
(a.output.parent/'mapping.json').write_text(json.dumps(rows,indent=2)+'\n')
(a.output.parent/'download-summary.json').write_text(json.dumps(dict(two_k_bytes=size2,resolution=res,download_bytes=sum(v['size'] for r in rows for v in r['maps'].values()),materials=len(rows)),indent=2)+'\n')
