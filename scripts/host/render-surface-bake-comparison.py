#!/usr/bin/env python3
"""Blender background rendering of production float maps, identical camera and mesh."""
import bpy, sys, pathlib, math
from mathutils import Vector
import numpy as np
root=pathlib.Path(sys.argv[sys.argv.index('--')+1])
variants=sys.argv[sys.argv.index('--')+2:] or ['before','after','blender']
bpy.ops.wm.read_factory_settings(use_empty=True)
scene=bpy.context.scene;scene.render.engine='CYCLES';scene.cycles.device='CPU';scene.cycles.samples=16
scene.render.threads_mode='FIXED';scene.render.threads=6
scene.render.resolution_x=scene.render.resolution_y=512;scene.render.resolution_percentage=100
scene.view_settings.view_transform='Standard';scene.view_settings.look='None'
scene.world=bpy.data.worlds.new('Background');scene.world.use_nodes=True
scene.world.node_tree.nodes['Background'].inputs[0].default_value=(.025,.025,.025,1)
scene.world.node_tree.nodes['Background'].inputs[1].default_value=1
for name in ['displaced','shaderball']:
    bpy.ops.object.select_all(action='SELECT');bpy.ops.object.delete(use_global=False)
    bpy.ops.wm.obj_import(filepath=str(root/'native/tests/fixtures'/f'{name}-evaluated.obj'),forward_axis='Y',up_axis='Z')
    objects=[o for o in scene.objects if o.type=='MESH'];bpy.context.view_layer.objects.active=objects[0]
    if len(objects)>1:bpy.ops.object.join()
    obj=bpy.context.object;bpy.ops.object.shade_smooth()
    bpy.ops.object.camera_add(location=(3,-4,2.7));camera=bpy.context.object
    camera.rotation_euler=(Vector((0,0,0))-camera.location).to_track_quat('-Z','Y').to_euler();camera.data.type='ORTHO';camera.data.ortho_scale=2.5;scene.camera=camera
    material=bpy.data.materials.new('Baked scalar');material.use_nodes=True;obj.data.materials.clear();obj.data.materials.append(material)
    nodes=material.node_tree.nodes;nodes.clear();links=material.node_tree.links
    out=nodes.new('ShaderNodeOutputMaterial');emission=nodes.new('ShaderNodeEmission');links.new(emission.outputs[0],out.inputs['Surface'])
    tex=nodes.new('ShaderNodeTexImage');tex.extension='EXTEND';links.new(tex.outputs['Color'],emission.inputs['Color'])
    for role in [3,4,6]:
        for variant in variants:
            path=root/variant/f'{name}-{role}.rgba' if variant!='blender' else root/'native/tests/fixtures'/f'blender-{name}-{role}.rgba'
            pixels=np.fromfile(path,dtype=np.float32).reshape(256,256,4)
            # Same fixed display gain for all curvature maps, never per-image normalization.
            if role==4:pixels[:,:,:3]=.5+3*(pixels[:,:,:3]-.5)
            image=bpy.data.images.new('float map',256,256,alpha=True,float_buffer=True);image.colorspace_settings.name='Non-Color';image.pixels.foreach_set(pixels.reshape(-1));tex.image=image
            scene.render.filepath=str(root/f'preview-{name}-{variant}-{role}.png');bpy.ops.render.render(write_still=True)
            bpy.data.images.remove(image)
