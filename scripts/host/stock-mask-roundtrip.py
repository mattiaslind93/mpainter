# SPDX-License-Identifier: GPL-3.0-or-later
"""Stock Blender proof for a packed scalar mask; run on the separate baseline executable."""
import array
import bpy
import json
from pathlib import Path
import sys
output=Path(sys.argv[sys.argv.index('--')+1]);output.mkdir(parents=True,exist_ok=True)
metadata=json.loads(bpy.context.scene['bts_document']);assert metadata['schema'] in [6,7]
texture_set=next(t for t in metadata['texture_sets'] if t['name']=='SphereSurface')
layer=texture_set['layers'][-1];assert layer['kind']=='fill' and layer['mask']['background']==0
image=bpy.context.scene['bts_layer_pixels'][layer['id']+'_5'];assert image.is_float and image.packed_file and image.size[:]==(1024,1024)
values=array.array('f',[0])*len(image.pixels);image.pixels.foreach_get(values)
assert max(values[0::4])>.5
for offset in range(0,len(values),4):values[offset]=0
image.pixels.foreach_set(values);image.update();image.pack()
bpy.ops.wm.save_as_mainfile(filepath=str(output/'stock-mask.blend'))
values[0:4]=array.array('f',[2,0,0,1]);image.pixels.foreach_set(values);image.update();image.pack()
bpy.ops.wm.save_as_mainfile(filepath=str(output/'invalid-mask.blend'))
(output/'stock.json').write_text(json.dumps({'blender_version':bpy.app.version_string,'layer_id':layer['id'],'packed_mask_image':image.name,'stock_edit':'grayscale set to zero, stroke alpha retained'},indent=2)+'\n')
