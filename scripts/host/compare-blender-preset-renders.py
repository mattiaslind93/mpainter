"""Assemble the real-mesh stock Blender renders; keep them distinct from UV proofs."""
import argparse
import json
from pathlib import Path
import numpy as np
from PIL import Image, ImageDraw
p=argparse.ArgumentParser(description=__doc__);p.add_argument('shots',type=Path);a=p.parse_args()
receipt=json.loads((a.shots/'mesh-render-results.json').read_text());records=[]
names={'oxide-flecks':'Rust','exposed-edges':'Edge Wear','crevice-sediment':'Crease Dirt',
       'organic-growth':'Moss','ground-contact':'Ground Dirt','surface-scuffs':'Scratches',
       'gravity-streaks':'Leaks & Stains','settled-dust':'Surface Dust','height-fade':'Position height',
       'curvature-ridge':'Curvature edges','rust-material':'Rust Material Preset'}
sheet=Image.new('RGB',(1536,316*6+40),'#202323');ImageDraw.Draw(sheet).text((12,12),'Real JL01 mesh: native fields | Blender nodes | difference x10 (same Cycles camera/light)',fill='white')
for i,slug in enumerate(receipt['rendered_presets']):
    left=Image.open(a.shots/(slug+'-mesh-native.png')).convert('RGB');right=Image.open(a.shots/(slug+'-mesh-blender.png')).convert('RGB')
    d=np.abs(np.array(left).astype(float)-np.array(right).astype(float))
    record={'preset':slug,'display_mean_error_8bit':float(d.mean()),'display_max_error_8bit':float(d.max())};records.append(record)
    row=Image.new('RGB',(768,316),'#202323');draw=ImageDraw.Draw(row);draw.text((8,5),names[slug],fill='white')
    for j,(im,label) in enumerate(zip((left,right,Image.fromarray(np.uint8(np.minimum(d*10,255)))),('mPainter fields / Cycles','Blender nodes / Cycles','Absolute difference x10'))):
        row.paste(im,(j*256,40));draw.text((j*256+8,22),label,fill='white')
    row.save(a.shots/(slug+'-mesh-comparison.png'));sheet.paste(row,((i%2)*768,(i//2)*316+40))
    assert record['display_mean_error_8bit']<.1,record
sheet.save(a.shots/'comparison-mesh.png')
receipt['render_comparisons']=records;(a.shots/'mesh-render-results.json').write_text(json.dumps(receipt,indent=2)+'\n')
print(json.dumps(records,indent=2))
