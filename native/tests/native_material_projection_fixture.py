# SPDX-License-Identifier: GPL-3.0-or-later
"""Owned BJ source: linked-coordinate native graphs, raw H, bitmap uniform.

Run only with the approved background Blender and private application paths.
This creates source fixtures; it does not execute the UI proof or inspect stock
materials. The OBJ really contains all three stated UV tiles and two owners.
"""
import bpy
import hashlib
import json
from pathlib import Path
import sys

out = Path(sys.argv[sys.argv.index('--') + 1]).resolve()
out.mkdir(parents=True, exist_ok=False)
bpy.ops.wm.read_factory_settings(use_empty=True)

def arithmetic(tree, operation, left, right):
    node = tree.nodes.new('ShaderNodeMath')
    node.operation = operation
    for index, value in enumerate((left, right)):
        if isinstance(value, (int, float)):
            node.inputs[index].default_value = value
        else:
            tree.links.new(value, node.inputs[index])
    return node.outputs[0]

for basis in ('UV', 'Object', 'Generated'):
    material = bpy.data.materials.new('BJ Native ' + basis)
    material.use_nodes = True
    material.use_fake_user = True
    tree = material.node_tree
    tree.nodes.clear()
    coordinates = tree.nodes.new('ShaderNodeTexCoord')
    group = bpy.data.node_groups.new('BJ coordinate H ' + basis, 'ShaderNodeTree')
    group.interface.new_socket(name='Coordinates', in_out='INPUT', socket_type='NodeSocketVector')
    for name, value in [('Bias', .5), ('Gain', 1.)]:
        socket = group.interface.new_socket(name=name, in_out='INPUT', socket_type='NodeSocketFloat')
        socket.default_value = value
    group.interface.new_socket(name='Shader', in_out='OUTPUT', socket_type='NodeSocketShader')
    inputs = group.nodes.new('NodeGroupInput')
    output = group.nodes.new('NodeGroupOutput')
    separate = group.nodes.new('ShaderNodeSeparateXYZ')
    group.links.new(inputs.outputs['Coordinates'], separate.inputs['Vector'])
    x, y, z = (separate.outputs[axis] for axis in ('X', 'Y', 'Z'))
    # Independent scalar channels; no shader rendering is the mathematical reference.
    rough = arithmetic(group, 'ADD', .15, arithmetic(group, 'ADD',
        arithmetic(group, 'MULTIPLY', .2, x), arithmetic(group, 'MULTIPLY', .1, y)))
    raw = arithmetic(group, 'ADD', arithmetic(group, 'ADD',
        arithmetic(group, 'MULTIPLY', .2, x), arithmetic(group, 'MULTIPLY', -.3, y)),
        arithmetic(group, 'MULTIPLY', .4, z))
    height = arithmetic(group, 'ADD', inputs.outputs['Bias'],
        arithmetic(group, 'MULTIPLY', inputs.outputs['Gain'], raw))
    bump = group.nodes.new('ShaderNodeBump')
    bump.inputs['Strength'].default_value = .25
    bump.inputs['Distance'].default_value = 1.
    bump.inputs['Filter Width'].default_value = 1.
    bump.invert = False
    group.links.new(height, bump.inputs['Height'])
    principled = group.nodes.new('ShaderNodeBsdfPrincipled')
    group.links.new(inputs.outputs['Coordinates'], principled.inputs['Base Color'])
    group.links.new(rough, principled.inputs['Roughness'])
    group.links.new(bump.outputs['Normal'], principled.inputs['Normal'])
    group.links.new(principled.outputs['BSDF'], output.inputs['Shader'])
    instance = tree.nodes.new('ShaderNodeGroup')
    instance.node_tree = group
    tree.links.new(coordinates.outputs[basis], instance.inputs['Coordinates'])
    surface = tree.nodes.new('ShaderNodeOutputMaterial')
    tree.links.new(instance.outputs['Shader'], surface.inputs['Surface'])

# A uniform-material asset can still contain a varying bitmap channel. Constant
# MaterialValues cannot establish that mapping edits change the actual output.
image = bpy.data.images.new('BJ exact bitmap', width=32, height=32, alpha=True)
image.colorspace_settings.name = 'Non-Color'
image.pixels = [v for y in range(32) for x in range(32)
                for v in ((x + .5) / 32, (y + .5) / 32,
                          .2 if (x // 4 + y // 4) % 2 else .8, 1.)]
image.filepath_raw = str(out / 'owned-uniform.png')
image.file_format = 'PNG'
image.save()
assert image.source == 'FILE'
image.pack()
# Packing does not make an absolute external filename safe as a cold-test
# fallback witness. Keep the stored Image path relative to the copied blend.
image.filepath = '//owned-uniform.png'
for packed in image.packed_files:
    packed.filepath = '//owned-uniform.png'
uniform = bpy.data.materials.new('BJ Uniform bitmap')
uniform.use_nodes = True
uniform.use_fake_user = True
tree = uniform.node_tree
texture = tree.nodes.new('ShaderNodeTexImage')
texture.image = image
tree.links.new(texture.outputs['Color'], tree.nodes.get('Principled BSDF').inputs['Base Color'])
# Separate ordinary Fill Bump source, not a native image-height extension.
height_image = bpy.data.images.new('BJ signed HDR ordinary Bump', width=32, height=32,
                                   alpha=True, float_buffer=True, is_data=True)
height_image.colorspace_settings.name = 'Non-Color'
height_image.pixels = [value for y in range(32) for x in range(32)
                       for value in (*([-.25 + 1.5 * (x + .5) / 32 + .125 * (y + .5) / 32] * 3), 1.)]
settings = bpy.context.scene.render.image_settings
settings.file_format = 'OPEN_EXR'
settings.color_mode = 'RGB'
settings.color_depth = '32'
settings.exr_codec = 'ZIP'
height_image.save_render(filepath=str(out / 'owned-height.exr'), scene=bpy.context.scene)
bpy.ops.wm.save_as_mainfile(filepath=str(out / 'owned-projection.blend'), relative_remap=False)
# Verify the serialized references from the owned file, not only pre-Save RNA.
bpy.ops.wm.open_mainfile(filepath=str(out / 'owned-projection.blend'), load_ui=False, use_scripts=False)
image = bpy.data.images['BJ exact bitmap']
assert image.source == 'FILE'
assert image.filepath == '//owned-uniform.png'
assert image.packed_file is not None and image.packed_file.size > 0
assert len(image.packed_files) == 1
assert all(packed.filepath == '//owned-uniform.png' for packed in image.packed_files)

lines = ['mtllib owned.mtl', 'usemtl BJ Surface']
vertex = uv = 1
for index, tile in enumerate((1001, 1002, 1011)):
    # Oblique surface normal (.6,0,.8), with a differently oriented second owner.
    if index < 2:
        lines += ['o ' + ('BJ Owner A' if index == 0 else 'BJ Owner B')]
    center = index * 2.5
    points = [(center-.8, -1., .6), (center+.8, -1., -.6),
              (center+.8, 1., -.6), (center-.8, 1., .6)]
    if index == 2:
        points = [(x, z, y) for x, y, z in points]
    lines += ['v %s %s %s' % point for point in points]
    u, v = (tile - 1001) % 10, (tile - 1001) // 10
    lines += ['vt %s %s' % (u+a, v+b) for a, b in ((0,0),(1,0),(1,1),(0,1))]
    lines += ['f ' + ' '.join(f'{vertex+i}/{uv+i}' for i in (0,1,2,3))]
    vertex += 4
    uv += 4
(out / 'owned.obj').write_text('\n'.join(lines) + '\n')
(out / 'owned.mtl').write_text('newmtl BJ Surface\nKd 0.5 0.5 0.5\n')
(out / 'fixture.json').write_text(json.dumps({
    'format': 'bts-native-projection-fixture', 'version': 1,
    'generator_sha256': hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
    'source_blend_sha256': hashlib.sha256((out / 'owned-projection.blend').read_bytes()).hexdigest(),
    'uniform_packed_image': {'filepath': image.filepath, 'source': image.source, 'verified_reopen': True,
                             'packed_paths': [packed.filepath for packed in image.packed_files],
                             'packed': bool(image.packed_file), 'packed_bytes': image.packed_file.size},
    'tiles': [1001,1002,1011], 'owner_names': ['BJ Owner A','BJ Owner B'],
    'native_materials': ['BJ Native UV','BJ Native Object','BJ Native Generated'],
    'uniform_material': 'BJ Uniform bitmap', 'bump_scope': 'raw H only; no native Bump parity',
    'roughness': '.15 + .2*X + .1*Y', 'height': 'Bias + Gain*(.2*X-.3*Y+.4*Z)',
    'ordinary_bump': '-.25 + 1.5*localU + .125*localV, RGB grayscale Float32 EXR; independent ordinary Fill channel',
    'uniform_data': '32x32 encoded PNG; compare decoded bytes, not ideal fractions'
}, indent=2) + '\n')
