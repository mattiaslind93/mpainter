# SPDX-License-Identifier: GPL-3.0-or-later
"""Small private two-tile image material; originals are never artist files."""
import bpy,json,sys
from pathlib import Path
args=sys.argv[sys.argv.index('--')+1:];size=int(args[1])if len(args)>1 else 16;assert 1<=size<=8192
out=Path(args[0]);out.mkdir(parents=True,exist_ok=True);images=out/'images';images.mkdir(exist_ok=True)
bpy.ops.wm.read_factory_settings(use_empty=True)
scene=bpy.context.scene;scene.render.image_settings.file_format='OPEN_EXR';scene.render.image_settings.color_mode='RGBA';scene.render.image_settings.color_depth='32';scene.render.image_settings.exr_codec='ZIP'
colors={1001:[.8,.1,.2,1],1002:[.1,.7,.3,1]}
for tile,color in colors.items():
 image=bpy.data.images.new('Source color '+str(tile),size,size,alpha=True,float_buffer=True);image.colorspace_settings.name='Linear Rec.709';image.pixels.foreach_set(color*(size*size));image.save_render(str(images/('color.%s.exr'%tile)),scene=scene);bpy.data.images.remove(image)
color=bpy.data.images.load(str(images/'color.1001.exr'));color.source='TILED';color.filepath='//images/color.<UDIM>.exr';color.tiles.new(1002);color.colorspace_settings.name='Linear Rec.709'
gray=bpy.data.images.new('Packed raw gray',size,size,alpha=True,float_buffer=False);gray.colorspace_settings.name='Non-Color';gray.pixels.foreach_set([.2,.2,.2,0]*(size*size));gray.filepath_raw=str(images/'gray.png');gray.file_format='PNG';gray.save();gray.pack()
material=bpy.data.materials.new('Image textures');material.use_nodes=True;material.use_fake_user=True;shader=material.node_tree.nodes.get('Principled BSDF')
for image,inputs in [(color,['Base Color']),(gray,['Metallic','Roughness'])]:
 node=material.node_tree.nodes.new('ShaderNodeTexImage');node.image=image;node.interpolation='Closest';node.extension='REPEAT'
 for name in inputs:material.node_tree.links.new(node.outputs['Color'],shader.inputs[name])
material.asset_mark();preview=material.preview_ensure();preview.image_size=(8,8);preview.image_pixels_float=[.2,.7,.3,1]*64
bad=bpy.data.materials.new('Linked image');bad.use_nodes=True;bad.use_fake_user=True;node=bad.node_tree.nodes.new('ShaderNodeTexImage');node.image=bpy.data.images.new('Generated unsupported',8,8);bad.node_tree.links.new(node.outputs['Color'],bad.node_tree.nodes.get('Principled BSDF').inputs['Base Color'])
missing=color.copy();missing.name='Missing declared tile';missing.tiles.new(1011)
for name,image,input in [('Missing tile',missing,'Base Color'),('Colored scalar',color,'Roughness')]:
 bad=bpy.data.materials.new(name);bad.use_nodes=True;bad.use_fake_user=True;node=bad.node_tree.nodes.new('ShaderNodeTexImage');node.image=image;node.interpolation='Closest';bad.node_tree.links.new(node.outputs['Color'],bad.node_tree.nodes.get('Principled BSDF').inputs[input])
bpy.ops.wm.save_as_mainfile(filepath=str(out/'materials.blend'))
# Packed source really has no readable original.
(images/'gray.png').unlink()
(out/'expected.json').write_text(json.dumps({'color_rec709':colors,'raw_gray':.2,'image_size':size,'source_images':2,'source_files':3,'packed_source_missing':True})+'\n')
