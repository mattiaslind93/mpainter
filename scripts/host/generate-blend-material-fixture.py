# SPDX-License-Identifier: GPL-3.0-or-later
"""Private fixture, generated before the host process starts."""
import bpy,json,sys
from pathlib import Path
out=Path(sys.argv[sys.argv.index('--')+1]);out.mkdir(parents=True,exist_ok=True)
bpy.ops.wm.read_factory_settings(use_empty=True)
material=bpy.data.materials.new('Copper constant');material.use_nodes=True;material.use_fake_user=True
shader=material.node_tree.nodes.get('Principled BSDF')
shader.inputs['Base Color'].default_value=(.6,.1,.025,1)
shader.inputs['Metallic'].default_value=.8;shader.inputs['Roughness'].default_value=.23
material.asset_mark()
preview=material.preview_ensure();preview.image_size=(8,8)
preview.image_pixels_float=[.9,.1,.05,1]*64
linked=bpy.data.materials.new('Linked image');linked.use_nodes=True;linked.use_fake_user=True
image=linked.node_tree.nodes.new('ShaderNodeTexImage');image.image=bpy.data.images.new('Image phase 2',8,8)
linked.node_tree.links.new(image.outputs['Color'],linked.node_tree.nodes.get('Principled BSDF').inputs['Base Color'])
bpy.ops.wm.save_as_mainfile(filepath=str(out/'materials.blend'))
(out/'expected.json').write_text(json.dumps({'color_rec709_linear':[.6,.1,.025,1],'metallic':.8,'roughness':.23})+'\n')
