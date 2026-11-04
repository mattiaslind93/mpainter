#!/usr/bin/env python3
"""Run with Blender --background --threads 6 --python this.py -- fixtures|reference DIR.
References use the exact evaluated/displaced triangles exported by mPainter's proof.
No render window or interactive UI is created.
"""
import bpy, sys, pathlib, math, json
import numpy as np
mode, directory = sys.argv[sys.argv.index('--')+1:]
root=pathlib.Path(directory); root.mkdir(parents=True,exist_ok=True)
bpy.ops.wm.read_factory_settings(use_empty=True)
if mode == 'fixtures':
    bpy.ops.mesh.primitive_uv_sphere_add(segments=32,ring_count=16)
    obj=bpy.context.object
    sub=obj.modifiers.new('Subdivision','SUBSURF');sub.levels=1
    bpy.ops.object.modifier_apply(modifier=sub.name)
    bpy.ops.object.shade_smooth()
    bpy.ops.wm.obj_export(filepath=str(root/'displaced.obj'),export_selected_objects=True,export_triangulated_mesh=True,forward_axis='Y',up_axis='Z')
    bpy.ops.object.delete()
    bpy.ops.wm.obj_import(filepath='/home/mlind/bts/blender-texture-studio/assets/shaderball/Shader_Ball_JL_01.obj',forward_axis='Y',up_axis='Z')
    objs=[o for o in bpy.context.scene.objects if o.type=='MESH']
    # Keep the asset's geometry/UVs; center and uniformly normalize world units.
    coords=[o.matrix_world@v.co for o in objs for v in o.data.vertices]
    lower=np.min([tuple(p) for p in coords],axis=0);upper=np.max([tuple(p) for p in coords],axis=0)
    center=(lower+upper)/2;scale=2/max(upper-lower)
    for o in objs:
        for v in o.data.vertices:v.co=tuple((np.array(tuple(o.matrix_world@v.co))-center)*scale)
        o.matrix_world.identity()
    bpy.ops.wm.obj_export(filepath=str(root/'shaderball.obj'),export_selected_objects=True,export_triangulated_mesh=True,forward_axis='Y',up_axis='Z')
else:
    scene=bpy.context.scene;scene.render.engine='CYCLES';scene.cycles.device='CPU';scene.cycles.samples=256
    scene.render.threads_mode='FIXED';scene.render.threads=6;scene.render.bake.margin=4
    scene.view_settings.view_transform='Standard';scene.view_settings.look='None'
    for name in ['displaced','shaderball']:
        bpy.ops.object.select_all(action='SELECT');bpy.ops.object.delete(use_global=False)
        bpy.ops.wm.obj_import(filepath=str(root/(name+'-evaluated.obj')),forward_axis='Y',up_axis='Z')
        objects=[o for o in bpy.context.scene.objects if o.type=='MESH']
        bpy.context.view_layer.objects.active=objects[0]
        if len(objects)>1:bpy.ops.object.join()
        obj=bpy.context.object;bpy.ops.object.shade_smooth()
        mat=bpy.data.materials.new(name);mat.use_nodes=True;obj.data.materials.clear();obj.data.materials.append(mat)
        nodes=mat.node_tree.nodes;nodes.clear();links=mat.node_tree.links
        out=nodes.new('ShaderNodeOutputMaterial');emit=nodes.new('ShaderNodeEmission');links.new(emit.outputs[0],out.inputs['Surface'])
        tex=nodes.new('ShaderNodeTexImage');nodes.active=tex
        extent=max(max(v.co[i] for v in obj.data.vertices)-min(v.co[i] for v in obj.data.vertices) for i in range(3))
        for role in [3,4,6]:
            image=bpy.data.images.new(f'{name}-{role}',256,256,alpha=True,float_buffer=True);image.colorspace_settings.name='Non-Color';tex.image=image
            if role==4:
                node=nodes.new('ShaderNodeNewGeometry');socket=node.outputs['Pointiness']
            else:
                node=nodes.new('ShaderNodeAmbientOcclusion');node.samples=256;node.inside=(role==6)
                node.inputs['Distance'].default_value=extent if role==3 else extent*.25
                socket=node.outputs['AO']
            links.new(socket,emit.inputs['Color']);bpy.ops.object.bake(type='EMIT')
            data=np.empty(256*256*4,dtype=np.float32);image.pixels.foreach_get(data);data.tofile(root/f'blender-{name}-{role}.rgba')
            image.filepath_raw=str(root/f'blender-{name}-{role}.exr');image.file_format='OPEN_EXR';image.save()
            nodes.remove(node);bpy.data.images.remove(image)
