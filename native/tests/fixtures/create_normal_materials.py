"""Run Blender in background with --python this.py -- OUTPUT_DIRECTORY."""
import json
import math
import sys
from pathlib import Path

import bpy
from mathutils import Vector

out = Path(sys.argv[sys.argv.index('--') + 1]).absolute()
out.mkdir(parents=True, exist_ok=True)
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.mesh.primitive_uv_sphere_add(segments=48, ring_count=24)
sphere = bpy.context.object
for polygon in sphere.data.polygons:
    polygon.use_smooth = True
bpy.ops.wm.obj_export(filepath=str(out / 'sphere.obj'), export_selected_objects=True, export_normals=True, export_materials=False)
bpy.ops.object.camera_add(location=(0, -4, 1))
camera = bpy.context.object
camera.rotation_euler = (Vector((0, 0, 0)) - camera.location).to_track_quat('-Z', 'Y').to_euler()
camera.data.type = 'ORTHO'
camera.data.ortho_scale = 2.5
scene = bpy.context.scene
scene.camera = camera
scene.render.engine = 'CYCLES'
scene.cycles.samples = 12
scene.cycles.device = 'CPU'
scene.render.resolution_x = scene.render.resolution_y = 128
scene.render.resolution_percentage = 100
scene.world = bpy.data.worlds.new('Studio')
scene.world.use_nodes = True
scene.world.node_tree.nodes['Background'].inputs['Color'].default_value = (.25, .25, .25, 1)
for location, power, size in [((-3, -4, 5), 800, 4), ((3, -2, 1), 500, 3)]:
    bpy.ops.object.light_add(type='AREA', location=location)
    light = bpy.context.object
    light.rotation_euler = (-light.location).to_track_quat('-Z', 'Y').to_euler()
    light.data.energy = power
    light.data.shape = 'DISK'
    light.data.size = size
image = bpy.data.images.new('Non-Color waves', width=64, height=64)
image.colorspace_settings.name = 'Non-Color'
pixels = []
for y in range(64):
    for x in range(64):
        nx = .3 * math.sin(x / 64 * math.tau * 5)
        ny = .3 * math.cos(y / 64 * math.tau * 5)
        nz = math.sqrt(1 - nx * nx - ny * ny)
        pixels.extend((.5 + .5 * nx, .5 + .5 * ny, .5 + .5 * nz, 1))
image.pixels[:] = pixels
image.filepath_raw = str(out / 'normal.png')
image.file_format = 'PNG'
image.save()
image = bpy.data.images.load(str(out / 'normal.png'))
image.colorspace_settings.name = 'Non-Color'
image.pack()
cases = []
for name in ('image-normal', 'procedural-bump', 'bump-normal-chain', 'coat-normal', 'object-normal', 'world-normal', 'diffuse-normal', 'unsupported-normal', 'constant-coat', 'unsupported-color', 'unsupported-closure-color', 'Brand Test - Turquoise Copper Noise'):
    material = bpy.data.materials.new(name)
    material.use_nodes = True
    material.use_fake_user = True
    nodes = material.node_tree.nodes
    links = material.node_tree.links
    bsdf = nodes.get('Principled BSDF')
    bsdf.inputs['Base Color'].default_value = (.02, .42, .5, 1)
    bsdf.inputs['Metallic'].default_value = .3
    bsdf.inputs['Roughness'].default_value = .35
    texture = nodes.new('ShaderNodeTexImage')
    texture.image = image
    normal = nodes.new('ShaderNodeNormalMap')
    normal.inputs['Strength'].default_value = 1 if name == 'image-normal' else .65
    if name == 'object-normal':
        normal.space = 'OBJECT'
    if name == 'world-normal':
        normal.space = 'WORLD'
    links.new(texture.outputs['Color'], normal.inputs['Color'])
    result = normal.outputs['Normal']
    if name in ('procedural-bump', 'bump-normal-chain'):
        noise = nodes.new('ShaderNodeTexNoise')
        noise.inputs['Scale'].default_value = 9
        bump = nodes.new('ShaderNodeBump')
        bump.inputs['Strength'].default_value = .4
        bump.inputs['Distance'].default_value = .06
        bump.invert = name == 'bump-normal-chain'
        links.new(noise.outputs['Fac'], bump.inputs['Height'])
        if name == 'bump-normal-chain':
            links.new(result, bump.inputs['Normal'])
        result = bump.outputs['Normal']
    if name == 'constant-coat':
        bsdf.inputs['Coat Weight'].default_value = 1
    elif name == 'unsupported-normal':
        coordinates = nodes.new('ShaderNodeTexCoord')
        links.new(coordinates.outputs['Reflection'], bsdf.inputs['Normal'])
    elif name == 'coat-normal':
        bsdf.inputs['Coat Weight'].default_value = 1
        links.new(result, bsdf.inputs['Coat Normal'])
    elif name == 'diffuse-normal':
        diffuse = nodes.new('ShaderNodeBsdfDiffuse')
        diffuse.inputs['Color'].default_value = (.02, .42, .5, 1)
        links.new(result, diffuse.inputs['Normal'])
        links.new(diffuse.outputs['BSDF'], nodes.get('Material Output').inputs['Surface'])
    else:
        links.new(result, bsdf.inputs['Normal'])
    if name == 'unsupported-closure-color':
        closure = nodes.new('ShaderNodeBsdfDiffuse')
        shader_color = nodes.new('ShaderNodeShaderToRGB')
        links.new(closure.outputs['BSDF'], shader_color.inputs['Shader'])
        links.new(shader_color.outputs['Color'], bsdf.inputs['Base Color'])
    if name == 'unsupported-color':
        geometry = nodes.new('ShaderNodeNewGeometry')
        links.new(geometry.outputs['True Normal'], bsdf.inputs['Base Color'])
    if name == 'Brand Test - Turquoise Copper Noise':
        # Reproduce the reported black-thumbnail material's original graph.
        nodes.clear()
        output = nodes.new('ShaderNodeOutputMaterial')
        bsdf = nodes.new('ShaderNodeBsdfPrincipled')
        bsdf.inputs['Metallic'].default_value = .35
        bsdf.inputs['Roughness'].default_value = .32
        coordinates = nodes.new('ShaderNodeTexCoord')
        noise = nodes.new('ShaderNodeTexNoise')
        noise.inputs['Scale'].default_value = 7
        noise.inputs['Detail'].default_value = 2
        ramp = nodes.new('ShaderNodeValToRGB')
        ramp.color_ramp.elements[0].position = .35
        ramp.color_ramp.elements[0].color = (.008, .32, .39, 1)
        ramp.color_ramp.elements[1].position = .65
        ramp.color_ramp.elements[1].color = (.8, .18, .035, 1)
        links.new(coordinates.outputs['UV'], noise.inputs['Vector'])
        links.new(noise.outputs['Fac'], ramp.inputs['Fac'])
        links.new(ramp.outputs['Color'], bsdf.inputs['Base Color'])
        links.new(bsdf.outputs['BSDF'], output.inputs['Surface'])
        material.asset_mark()
    sphere.data.materials.clear()
    sphere.data.materials.append(material)
    blend = out / (name + '.blend')
    bpy.ops.wm.save_as_mainfile(filepath=str(blend))
    scene.render.filepath = str(out / (name + '-blender.png'))
    bpy.ops.render.render(write_still=True)
    cases.append({'file': str(blend), 'material': name})
(out / 'cases.json').write_text(json.dumps(cases, indent=2))
print('NORMAL_FIXTURES_OK', len(cases))
