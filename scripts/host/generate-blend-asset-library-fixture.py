"""Read-only Asset Library fixture; no default library configuration is touched."""
import bpy,json,os,sys
from pathlib import Path
out=Path(sys.argv[sys.argv.index('--')+1]);root=out/'library';root.mkdir(parents=True,exist_ok=True);(root/'nested').mkdir(exist_ok=True)
metal='1a1b1c1d-0001-4000-8000-000000000001';plastic='1a1b1c1d-0002-4000-8000-000000000002'
(root/'blender_assets.cats.txt').write_text('# Private fixture\nVERSION 1\n'+metal+':Materials/Metals:Metals\n'+plastic+':Materials/Plastics:Plastics\n')
bpy.ops.wm.read_factory_settings(use_empty=True)
scene=bpy.context.scene;scene.render.image_settings.file_format='OPEN_EXR';scene.render.image_settings.color_mode='RGBA';scene.render.image_settings.color_depth='32'
image=bpy.data.images.new('Library color',8,8,alpha=True,float_buffer=True);image.colorspace_settings.name='Linear Rec.709';image.pixels.foreach_set([.3,.1,.7,1]*64);image.save_render(str(root/'swatch.exr'),scene=scene);bpy.data.images.remove(image)
image=bpy.data.images.load(str(root/'swatch.exr'));image.colorspace_settings.name='Linear Rec.709'
def material(name,color,catalog='',tags=(),marked=True,preview=False,texture=None):
 m=bpy.data.materials.new(name);m.use_fake_user=True;m.use_nodes=True;bsdf=m.node_tree.nodes.get('Principled BSDF');bsdf.inputs['Base Color'].default_value=(*color,1);bsdf.inputs['Roughness'].default_value=.4
 if marked:
  m.asset_mark();m.asset_data.catalog_id=catalog or '00000000-0000-0000-0000-000000000000';m.asset_data.description='Private material '+name
  for tag in tags:m.asset_data.tags.new(tag)
 if preview:
  p=m.preview_ensure();p.image_size=(8,8);p.image_pixels_float=[.8,.12,.04,1]*64
 if texture:
  node=m.node_tree.nodes.new('ShaderNodeTexImage');node.image=texture;m.node_tree.links.new(node.outputs['Color'],bsdf.inputs['Base Color'])
 return m
material('Copper asset',(.7,.12,.03),metal,['warm','metal'],preview=True)
material('Matte generated',(.12,.35,.6),plastic,['matte'])
material('Textured generated',(1,1,1),plastic,['fabric','blue'],texture=image)
packed=image.copy();packed.name='Packed preview';packed.pack();material('Packed generated',(1,1,1),tags=['packed'],texture=packed)
material('Unassigned asset',(.2,.2,.2),tags=['neutral'])
material('Unmarked',(.4,.4,.4),marked=False)
bpy.ops.wm.save_as_mainfile(filepath=str(root/'materials.blend'))
bpy.ops.wm.read_factory_settings(use_empty=True);material('Copper asset',(.5,.3,.03),metal,['gold']);bpy.ops.wm.save_as_mainfile(filepath=str(root/'nested/extra.blend'))
trap=out/'outside';trap.mkdir(exist_ok=True);(trap/'forbidden.blend').write_bytes(b'not a library');os.symlink(trap,root/'outside-link',target_is_directory=True)
cancel=out/'cancel-library';cancel.mkdir(exist_ok=True)
for i in range(500):os.link(root/'materials.blend',cancel/('%03d.blend'%i))
(out/'expected.json').write_text(json.dumps({'root':str(root),'files':2,'all_materials':7,'marked_materials':6,'generated_previews':5,'catalogs':[metal,plastic],'texture_rec709':[.3,.1,.7]})+'\n')
