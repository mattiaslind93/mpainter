# SPDX-License-Identifier: GPL-3.0-or-later
"""Known authored pixels and a linked graph whose socket default is misleading."""
import bpy, sys
from pathlib import Path
out = Path(sys.argv[sys.argv.index('--') + 1]).resolve()
out.parent.mkdir(parents=True, exist_ok=True)
bpy.ops.wm.read_factory_settings(use_empty=True)
for name in ('BG Saved', 'BG No preview'):
    material = bpy.data.materials.new(name)
    material.use_nodes = True
    material.use_fake_user = True
    shader = material.node_tree.nodes.get('Principled BSDF')
    shader.inputs['Base Color'].default_value = (.01, .01, .8, 1)
    shader.inputs['Roughness'].default_value = .65
    if name == 'BG Saved':
        preview = material.preview_ensure()
        preview.image_size = (13, 9)
        preview.image_pixels_float = [c for y in range(9) for x in range(13)
                                     for c in (x % 3 == 0, (8-y) % 3 == 0, (x+y) % 2 == 1, 1)]
    else:
        rgb = material.node_tree.nodes.new('ShaderNodeRGB')
        rgb.outputs[0].default_value = (.8, .002, .002, 1)
        material.node_tree.links.new(rgb.outputs[0], shader.inputs['Base Color'])
bpy.ops.wm.save_as_mainfile(filepath=str(out))
