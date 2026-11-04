"""Run with the unmodified Blender baseline, never as an application worker."""
import array
import bpy
import json
from pathlib import Path
import sys

output = Path(sys.argv[sys.argv.index('--') + 1])
output.mkdir(parents=True, exist_ok=True)
scene = bpy.context.scene
metadata = json.loads(scene['bts_document'])
assert metadata['schema'] in [1,2,3,4,5,6,7]
original_schema=metadata['schema']
assert len([o for o in scene.objects if o.type == 'MESH']) == 2
assert scene['bts_environment'].packed_file is not None
assert scene.world.node_tree is not None
sets = scene['bts_sets']
pixels = scene['bts_layer_pixels']
for texture_set in metadata['texture_sets']:
    material = sets[texture_set['id']]
    assert material.use_nodes
    assert material.node_tree.nodes['__BTS_ViewSwitch'].inputs[0].default_value == 0
    material.name += '_Renamed'
for stored_image in bpy.data.images:
    stored_image.name += '_Renamed'
painted = next(t for t in metadata['texture_sets'] if t['name'] == 'SphereSurface')['layers'][-1]
image = pixels[painted['id'] + '_2']
assert image.is_float and image.packed_file is not None
assert image.size[:] == (1024, 1024)
values = array.array('f', [0]) * len(image.pixels)
image.pixels.foreach_get(values)
offset = (512 * 1024 + 512) * 4
before = list(values[offset:offset+4])
values[offset:offset+4] = array.array('f', [.123456, 0, 0, 1])
image.pixels.foreach_set(values)
image.update()
image.pack()
bpy.ops.wm.save_as_mainfile(filepath=str(output/'stock-edited.blend'))
expected = list(values[offset:offset+4])
metadata['schema'] = 999
scene['bts_document'] = json.dumps(metadata)
bpy.ops.wm.save_as_mainfile(filepath=str(output/'unsupported.blend'))
metadata['schema'] = original_schema
scene['bts_document'] = json.dumps(metadata)
values[offset+3] = 2
image.pixels.foreach_set(values)
image.update()
image.pack()
bpy.ops.wm.save_as_mainfile(filepath=str(output/'invalid-pixels.blend'))
result = {'blender_version': bpy.app.version_string, 'build_hash': bpy.app.build_hash.decode(),
          'mesh_objects': 2, 'set_ids': list(sets.keys()), 'layer_id': painted['id'],
          'author_image': image.name, 'roughness_pixel_before': before,
          'roughness_pixel_after': expected,
          'packed_author_images': len(pixels), 'packed_environment': True,
          'standard_surface_shaders': True, 'material_renames_preserve_id_references': True}
(output/'stock.json').write_text(json.dumps(result, indent=2)+'\n')
print('BTS_STOCK_ROUNDTRIP_PASS', json.dumps(result))
