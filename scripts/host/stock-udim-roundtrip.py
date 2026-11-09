# SPDX-License-Identifier: GPL-3.0-or-later
"""Validate ordinary packed UDIM shaders in the unmodified Blender baseline."""
import bpy
import json
from pathlib import Path
import sys
output=Path(sys.argv[sys.argv.index('--')+1]);output.mkdir(parents=True,exist_ok=True)
scene=bpy.context.scene
metadata=json.loads(scene['bts_document'])
assert metadata['schema'] in [2,3,4,5,6,7] and len(metadata['texture_sets'])==1
texture_set=metadata['texture_sets'][0];assert texture_set['tiles']==[1001,1002]
material=scene['bts_sets'][texture_set['id']]
images=[]
for c in range(5):
    image=material.node_tree.nodes[f'__BTS_Pixels{c}'].image
    assert image.source=='TILED' and [tile.number for tile in image.tiles]==[1001,1002]
    assert len(image.packed_files)==2
    images.append({'channel':c,'tiles':[tile.number for tile in image.tiles],'packed_files':len(image.packed_files)})
assert len(scene['bts_layer_pixels'])==4
assert all(image.packed_file is not None for image in scene['bts_layer_pixels'].values())
assert scene['bts_environment'].packed_file is not None
bpy.ops.wm.save_as_mainfile(filepath=str(output/'stock.blend'))
result={'version':bpy.app.version_string,'build_hash':bpy.app.build_hash.decode(),'images':images,'packed_author_images':4,'metadata':metadata}
(output/'stock.json').write_text(json.dumps(result,indent=2)+'\n')
print('BTS_STOCK_UDIM_PASS')
