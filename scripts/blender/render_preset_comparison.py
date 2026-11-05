"""Render the real JL01 mesh in stock Blender, comparing native fields and shader trees."""
import argparse
import base64
import json
from pathlib import Path
import sys
sys.path.insert(0,str(Path(__file__).resolve().parent))
import bpy
import numpy as np
from mathutils import Vector, Matrix
import mpainter_presets as bridge
from preset_bake import bake_socket
from verify_presets import plane

p=argparse.ArgumentParser();p.add_argument('catalog',type=Path);p.add_argument('validation',type=Path);p.add_argument('shots',type=Path)
a=p.parse_args(sys.argv[sys.argv.index('--')+1:])
scene=bpy.context.scene;scene.render.engine='CYCLES';scene.cycles.device='CPU';scene.cycles.samples=8
scene.cycles.use_denoising=False;scene.cycles.seed=1234
scene.render.threads_mode='FIXED';scene.render.threads=2
scene.render.resolution_x=scene.render.resolution_y=256;scene.render.resolution_percentage=100
scene.view_settings.view_transform='Standard';scene.view_settings.look='None'
scene.render.image_settings.file_format='PNG';scene.render.image_settings.color_mode='RGB';scene.render.film_transparent=False
scene.world=bpy.data.worlds.new('Comparison world');scene.world.use_nodes=True
scene.world.node_tree.nodes.get('Background').inputs[0].default_value=(.18,.18,.18,1)
scene.world.node_tree.nodes.get('Background').inputs[1].default_value=.7
obj=bpy.data.objects['Shader_Ball_JL_01']
# Verify actual OBJ UVs in a fresh Blender process, independently of the builder.
repo=Path(__file__).resolve().parents[2]
uvs=[];indices=[];vertices=0
for line in (repo/'assets/meshes/Shader_Ball_JL_01.obj').read_text().splitlines():
    fields=line.split()
    if not fields:continue
    if fields[0]=='vt':uvs.append([float(x) for x in fields[1:3]])
    elif fields[0]=='v':vertices+=1
    elif fields[0]=='f':indices.extend(int(x.split('/')[1])-1 for x in fields[1:])
actual=np.array([x.uv[:] for x in obj.data.uv_layers['UVMap'].data],dtype=np.float32)
assert np.array_equal(actual,np.array(uvs,dtype=np.float32)[indices])
assert len(obj.data.vertices)==vertices
assert all(n.image and n.image.packed_file and n.image.colorspace_settings.name=='Non-Color'
           for t in bpy.data.node_groups for n in t.nodes if n.type=='TEX_IMAGE')
assert {1,2,3,4,5,6}.issubset({t.get('mp_role') for t in bpy.data.node_groups})
grouped=a.validation/'grouped'
snapshot=bridge.import_package(grouped/'native/preset.json','Nested native group')
assert snapshot.get('mp_note','').startswith('BAKED WHOLE PRESET REFERENCE')
unchanged=bridge.export_package(snapshot,grouped/'blender.json')
assert base64.b64decode(unchanged['archive'])==(grouped/'source.btsmask').read_bytes()
# Complete the lossy-graph roundtrip by reading the native archives back as nodes.
probe=plane();cold=[]
for kind in ('mask','material'):
    folder=a.validation/('arbitrary-'+kind)/'native';root=bridge.import_package(folder/'preset.json','Cold baked '+kind)
    assert 'mp_note' not in root, 'Baked scalar/PBR fills should be real image nodes'
    mat=bridge.material_for(root);group=next(n for n in mat.node_tree.nodes if n.type=='GROUP')
    data=json.loads((folder/'preset.json').read_text())
    for name,key in ([('Mask',None)] if kind=='mask' else [('Base Color','color'),('Roughness','roughness'),('Metallic','metallic')]):
        values=bake_socket(probe,mat,group.outputs[name]);desc=data['mask']['reference'] if key is None else data['reference_'+key]
        expected=np.fromfile(folder/desc['file'],dtype='<f4').reshape(256,256,4)
        delta=np.abs(values[...,:3]-expected[...,:3]);error=float(delta.max());assert error<1e-6,(kind,name,error)
        cold.append({'kind':kind,'channel':name,'maximum_absolute_error':error})
    bpy.data.materials.remove(mat)
bpy.data.objects.remove(probe,do_unlink=True)
scene.cycles.samples=8
camera_data=bpy.data.cameras.new('Comparison camera');camera=bpy.data.objects.new('Comparison camera',camera_data)
bpy.context.collection.objects.link(camera);camera.location=(3,2.2,4)
z=camera.location.normalized();x=Vector((0,1,0)).cross(z).normalized();y=z.cross(x)
camera.rotation_euler=Matrix((x,y,z)).transposed().to_euler();camera_data.type='ORTHO';camera_data.ortho_scale=2.5;scene.camera=camera
light_data=bpy.data.lights.new('Comparison softbox','AREA');light_data.energy=450;light_data.size=4
light=bpy.data.objects.new('Comparison softbox',light_data);bpy.context.collection.objects.link(light);light.location=(2,4,3)
light.rotation_euler=(-light.location).to_track_quat('-Z','Y').to_euler()
records=[]
for slug in bridge.SLUGS+['rust-material']:
    data=json.loads((a.catalog/slug/'preset.json').read_text())
    scene.cycles.samples=64 if data['kind']=='material' else 8
    root=next(t for t in bpy.data.node_groups if t.get('mp_slug')==slug)
    actual_mat=bridge.material_for(root)
    native_mat=bpy.data.materials.new('Native values '+slug);native_mat.use_nodes=True
    nodes,links=native_mat.node_tree.nodes,native_mat.node_tree.links;nodes.clear()
    out=nodes.new('ShaderNodeOutputMaterial')
    shader=nodes.new('ShaderNodeEmission' if data['kind']=='mask' else 'ShaderNodeBsdfPrincipled');links.new(shader.outputs[0],out.inputs['Surface'])
    references=[('Color',data['mask']['reference'])] if data['kind']=='mask' else [
        ('Base Color',data['reference_color']),('Roughness',data['reference_roughness']),('Metallic',data['reference_metallic'])]
    for channel,desc in references:
        image=bridge.raw_image(a.catalog/slug,desc,'Native rendered '+slug+' '+channel)
        tex=nodes.new('ShaderNodeTexImage');tex.image=image;tex.interpolation='Closest';tex.extension='EXTEND'
        uv=nodes.new('ShaderNodeUVMap');uv.uv_map='UVMap';links.new(uv.outputs[0],tex.inputs['Vector']);links.new(tex.outputs['Color'],shader.inputs[channel])
    for label,mat in [('native',native_mat),('blender',actual_mat)]:
        obj.data.materials.clear();obj.data.materials.append(mat)
        scene.render.filepath=str(a.shots/(slug+'-mesh-'+label+'.png'))
        bpy.ops.render.render(write_still=True)
    records.append(slug)
    bpy.data.materials.remove(native_mat);bpy.data.materials.remove(actual_mat)
(a.shots/'mesh-render-results.json').write_text(json.dumps({'blender':bpy.app.version_string,'renderer':'Cycles CPU, 8 mask / 64 material samples, 2 threads, shared camera/light/display',
    'mesh':'Shader_Ball_JL_01','source_uvs_exact':True,'source_vertex_count':vertices,'source_loop_count':len(indices),
    'packed_non_color_textures':True,'input_roles':[1,2,3,4,5,6],'nested_mask_snapshot_preserved':True,'native_bake_to_blender':cold,'rendered_presets':records},indent=2)+'\n')
print('PASS: source UVs, packed inputs, native bake cold reimport and real-mesh renders',flush=True)
