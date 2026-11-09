#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Compose measured images; never synthesize or retouch bake output."""
import pathlib,sys,json,subprocess
import numpy as np
from PIL import Image,ImageDraw,ImageFont
root=pathlib.Path(sys.argv[1]);dest=pathlib.Path(sys.argv[2]);dest.mkdir(parents=True,exist_ok=True)
font=ImageFont.truetype(subprocess.check_output(['fc-match','-f','%{file}','sans'],text=True).strip(),22)
metrics=[]
for role,label in [(4,'curvature'),(3,'ao'),(6,'thickness')]:
    canvas=Image.new('RGB',(1536,1140),(22,24,25));draw=ImageDraw.Draw(canvas)
    for col,title in enumerate(['Before','After','Blender Cycles Pointiness' if role==4 else 'Blender Cycles AO (inside)' if role==6 else 'Blender Cycles AO']):draw.text((col*512+16,12),title,font=font,fill='white')
    for row,name in enumerate(['displaced','shaderball']):
        for col,variant in enumerate(['before','after','blender']):canvas.paste(Image.open(root/f'preview-{name}-{variant}-{role}.png').convert('RGB'),(col*512,48+row*540))
        draw.text((16,562+row*540),'Subdivided sphere + height displacement' if row==0 else 'Shader_Ball_JL_01',font=font,fill='white')
        ref=np.fromfile(root/'native/tests/fixtures'/f'blender-{name}-{role}.rgba',dtype=np.float32).reshape(256,256,4)
        for variant in ['before','after']:
            p=np.fromfile(root/variant/f'{name}-{role}.rgba',dtype=np.float32).reshape(256,256,4);mask=(p[:,:,3]>.99)&(ref[:,:,3]>.99)
            delta=p[:,:,0][mask]-ref[:,:,0][mask]
            metrics.append(dict(mesh=name,map=label,variant=variant,mae=float(abs(delta).mean()),rmse=float(np.sqrt((delta*delta).mean())),minimum=float(p[:,:,0][mask].min()),maximum=float(p[:,:,0][mask].max()),unique_float_values=int(len(np.unique(p[:,:,0][mask]))),texels=int(mask.sum())))
    if role==4:draw.text((620,1110),'Fixed curvature display gain: 3x around 0.5',font=font,fill='white')
    canvas.save(dest/f'{label}-compare.png')
(root/'comparison-metrics.json').write_text(json.dumps(metrics,indent=2)+'\n')
