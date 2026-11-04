#!/usr/bin/env python3
"""Compare native headless HDR renders, keep theme backgrounds out of the metric.

Requires NumPy, Pillow and SciPy. See verify-environment-orientation.py for the
native driver. RGBE probes independently check the thumbnail sampler against
Blender's camera -> Z-up -> +90 degree -> equirectangular equations.
"""
import argparse
import json
from pathlib import Path
import numpy as np
from PIL import Image, ImageDraw, ImageFont
from scipy.ndimage import binary_fill_holes, binary_erosion

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--reference',type=Path,required=True)
p.add_argument('--candidate',type=Path,required=True)
p.add_argument('--output',type=Path,required=True)
a=p.parse_args()
proof={}
for name in ('eevee','cycles','world','orbit'):
    left=np.array(Image.open(a.reference/f'{name}.png').convert('RGB')).astype(float)
    right=np.array(Image.open(a.candidate/f'{name}.png').convert('RGB')).astype(float)
    # Reference background is gray in both engines. Fill holes in
    # the silhouette and remove its antialiased boundary before comparing.
    mask=binary_erosion(binary_fill_holes(np.max(abs(left-left[0,0]),axis=2)>5),iterations=4)
    delta=abs(left-right)[mask]
    metric={'object_pixels':int(mask.sum()),'mean_absolute_rgb_255':float(delta.mean()),
            'p99_rgb_255':float(np.percentile(delta,99)),'maximum_rgb_255':float(delta.max())}
    assert mask.sum()>100000 and metric['mean_absolute_rgb_255']<.15 and metric['p99_rgb_255']<=2,(name,metric)
    proof[name]=metric
state=json.loads((a.candidate/'proof.json').read_text())
for name in ('environment','reopened','legacy_reopened'):
    assert abs(state[name]['rotation']-90)<1e-4,(name,state[name])
    assert state[name]['opacity']==0
assert state['authored_zero_reopened']['rotation']==0
assert abs(state['legacy_authored_reopened']['rotation']-37)<1e-4
assert state['legacy_authored_reopened']['alignment']=='World'

root=Path(__file__).resolve().parents[2]
rgbe=np.array(Image.open(root/'assets/mask-preview/preview-studio-0.png')).astype(float)
rgb=rgbe[:,:,:3]*np.where(rgbe[:,:,3]>0,np.exp2(rgbe[:,:,3]-136),0)[:,:,None]
h,w=rgb.shape[:2]
errors=[]
for probe in state['thumbnail_samples']:
    x,y,z=probe['direction']
    # Independent explicit transforms: camera handedness -> Z-up -> Rz(+90).
    direction=np.array([x,-z,y]);direction=np.array([-direction[1],direction[0],direction[2]])
    u=.5-np.arctan2(direction[1],direction[0])/(2*np.pi)
    v=np.arccos(np.clip(direction[2],-1,1))/np.pi
    fx,fy=u*w-.5,v*h-.5;ix,iy=int(np.floor(fx)),int(np.floor(fy))
    expected=np.zeros(3)
    for dy in range(2):
        for dx in range(2):
            expected+=rgb[np.clip(iy+dy,0,h-1),(ix+dx)%w]*(fx-ix if dx else 1-fx+ix)*(fy-iy if dy else 1-fy+iy)
    errors.append(float(np.max(abs(expected-probe['rgb']))))
assert len(errors)==24 and max(errors)<1e-10,errors
proof['thumbnail_mapping']={'directions':len(errors),'max_linear_rgb_error':max(errors),'rotation_degrees':90,'mirror':False}
proof['persistence']={key:state[key]['rotation'] for key in ('reopened','legacy_reopened','authored_zero_reopened','legacy_authored_reopened')}
# The camera never shows HDR pixels. EEVEE uses the theme gradient; Cycles the
# theme's neutral center gray. Verify the uncluttered corners of the latter.
cycles=np.array(Image.open(a.candidate/'cycles.png').convert('RGB'))
assert np.max(abs(cycles[:80,:80].astype(int)-114))<=1
proof['cycles_background_rgb']=cycles[0,0].tolist()
canvas=Image.new('RGB',(1536,1632),'#303030');draw=ImageDraw.Draw(canvas);font=ImageFont.load_default(size=20)
for row,name in enumerate(('eevee','cycles')):
    y=row*816
    draw.text((20,y+15),f'mfPainter - {name.upper()} - 90 deg',fill='white',font=font)
    draw.text((788,y+15),f'mPainter - {name.upper()} - 90 deg',fill='white',font=font)
    for col,folder in enumerate((a.reference,a.candidate)):
        canvas.paste(Image.open(folder/f'{name}.png').convert('RGB'),(col*768,y+48))
a.output.parent.mkdir(parents=True,exist_ok=True);canvas.save(a.output)
proof['passed']=True
(a.output.with_suffix('.json')).write_text(json.dumps(proof,indent=2)+'\n')
print(json.dumps(proof,indent=2))
