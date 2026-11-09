# SPDX-License-Identifier: GPL-3.0-or-later
"""Own native .blend source for object/collection append verification."""
import bpy, json, sys
from pathlib import Path

out=Path(sys.argv[sys.argv.index('--')+1]);out.mkdir(parents=True,exist_ok=True)
bpy.ops.object.select_all(action='SELECT');bpy.ops.object.delete(use_global=False)
for collection in list(bpy.data.collections):bpy.data.collections.remove(collection)
character=bpy.data.collections.new('Character');bpy.context.scene.collection.children.link(character)
nested=bpy.data.collections.new('Nested');character.children.link(nested)
unrelated=bpy.data.collections.new('Unrelated');bpy.context.scene.collection.children.link(unrelated)
parent=bpy.data.objects.new('Root',None);character.objects.link(parent)
parent.location=(2,0,0);parent.keyframe_insert('location',frame=1)
parent.location=(4,0,0);parent.keyframe_insert('location',frame=7)
def cube(name,collection,location,tile,gray):
 bpy.ops.mesh.primitive_cube_add(size=1,location=(0,0,0));obj=bpy.context.object;obj.name=name
 for old in list(obj.users_collection):old.objects.unlink(obj)
 collection.objects.link(obj);obj.parent=parent;obj.location=location
 for uv in obj.data.uv_layers.active.data:uv.uv=(uv.uv.x*.8+.1+tile,uv.uv.y*.8+.1)
 material=bpy.data.materials.new('Surface_'+name);material.use_nodes=True
 shader=material.node_tree.nodes.get('Principled BSDF');shader.inputs['Base Color'].default_value=(gray,gray,gray,1);shader.inputs['Roughness'].default_value=.35
 obj.data.materials.append(material)
 return obj
body=cube('Body',character,(-1,0,0),0,.25)
body.modifiers.new('Source subdivision omitted for painting cage','SUBSURF').levels=3
body.shape_key_add(name='Basis');morph=body.shape_key_add(name='Source morph omitted');morph.data[0].co.x+=2;morph.value=1
eye=cube('Öga',nested,(1,0,0),1,.6)
outside=cube('Not selected',unrelated,(20,0,0),0,.9)
camera=bpy.data.objects.new('ReferenceCamera',bpy.data.cameras.new('ReferenceCamera'))
character.objects.link(camera);camera.location=(0,-8,4)
bpy.context.scene.camera=camera;bpy.context.scene.frame_set(7)
bpy.ops.wm.save_as_mainfile(filepath=str(out/'library.blend'))
(out/'expected.json').write_text(json.dumps({'collection':'Character','objects':['Body','Öga'],'vertices_per_mesh':8,'triangles_per_mesh':12,'world_x_frame_1':{'Body':1,'Öga':3},'tile_by_material':{'Surface_Body':1001,'Surface_Öga':1002}},indent=2)+'\n')
