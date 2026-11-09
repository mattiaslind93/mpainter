# SPDX-License-Identifier: GPL-3.0-or-later
"""Explicit, lossy shader graph -> native texture input export. Blender background only.

Import bake_socket() for a specific graph socket, or use:
blender -b scene.blend --python preset_bake.py -- --material NAME --kind mask --output DIR
Mask mode bakes the active Material Output surface's emission. Material mode
bakes a directly connected Principled BSDF's Base Color/Roughness/Metallic inputs.
"""
import argparse
import json
from pathlib import Path
import sys
import bpy
import numpy as np


def bake_socket(obj, material, source, side=256):
    """Bake a socket (or constant RGB/scalar) with Cycles, restoring scene ownership."""
    if not bpy.app.background: raise RuntimeError('Use blender -b')
    if not obj.data.uv_layers: raise ValueError('Bake requires mesh UVs')
    scene = bpy.context.scene
    scene.render.engine = 'CYCLES'; scene.cycles.device = 'CPU'; scene.cycles.samples = 1
    scene.render.threads_mode = 'FIXED'; scene.render.threads = 2
    scene.render.bake.margin = 0; scene.render.bake.use_clear = True
    nodes, links = material.node_tree.nodes, material.node_tree.links
    output = next(n for n in nodes if n.type == 'OUTPUT_MATERIAL' and n.is_active_output)
    previous = [(l.from_socket, l.to_socket) for l in output.inputs['Surface'].links]
    old_materials = list(obj.data.materials)
    old_indices = [p.material_index for p in obj.data.polygons]
    old_active = bpy.context.view_layer.objects.active
    selected = list(bpy.context.selected_objects)
    active_node = nodes.active
    image = bpy.data.images.new('mPainter bake working image', width=side, height=side, float_buffer=True)
    image.colorspace_settings.name = 'Non-Color'; image.alpha_mode = 'CHANNEL_PACKED'
    image_node = nodes.new('ShaderNodeTexImage'); image_node.image = image
    emission = None
    try:
        if source is not None:
            emission = nodes.new('ShaderNodeEmission')
            if isinstance(source, bpy.types.NodeSocket): links.new(source, emission.inputs['Color'])
            else: emission.inputs['Color'].default_value = (source, source, source, 1) if isinstance(source, (int, float)) else source
            links.new(emission.outputs[0], output.inputs['Surface'])
        for item in selected: item.select_set(False)
        obj.select_set(True); bpy.context.view_layer.objects.active = obj
        obj.data.materials.clear(); obj.data.materials.append(material)
        for poly in obj.data.polygons: poly.material_index = 0
        for node in nodes: node.select = False
        image_node.select = True; nodes.active = image_node
        bpy.ops.object.bake(type='EMIT')
        values = np.empty(side*side*4, dtype=np.float32); image.pixels.foreach_get(values)
        if not np.isfinite(values).all(): raise ValueError('Non-finite baked output')
        return values.reshape(side, side, 4)
    finally:
        nodes.remove(image_node)
        if emission: nodes.remove(emission)
        for a, b in previous: links.new(a, b)
        nodes.active = active_node
        obj.data.materials.clear()
        for mat in old_materials: obj.data.materials.append(mat)
        for poly, index in zip(obj.data.polygons, old_indices): poly.material_index = index
        obj.select_set(False)
        for item in selected: item.select_set(True)
        bpy.context.view_layer.objects.active = old_active
        bpy.data.images.remove(image)


def save_float(output, name, values):
    output = Path(output); output.mkdir(parents=True, exist_ok=True)
    values.astype('<f4').tofile(output / (name + '.f32'))
    return {'file': name + '.f32', 'width': values.shape[1], 'height': values.shape[0],
            'channels': 4, 'origin': 'bottom-left', 'encoding': 'float32-le-linear'}


def bake_material(obj, material, output, kind='mask', side=256):
    root = {'bake_version': 1, 'kind': kind, 'note': 'Explicit lossy UV bake; original Blender graph is not a native procedural preset'}
    if kind == 'mask':
        values = bake_socket(obj, material, None, side)
        if values[..., 0].min() < 0 or values[..., 0].max() > 1: raise ValueError('Mask output must be in [0,1]')
        root['mask'] = save_float(output, 'mask', values)
    else:
        nodes = material.node_tree.nodes
        out = next(n for n in nodes if n.type == 'OUTPUT_MATERIAL' and n.is_active_output)
        if len(out.inputs['Surface'].links) != 1 or out.inputs['Surface'].links[0].from_node.type != 'BSDF_PRINCIPLED':
            raise ValueError('Material export requires a directly connected Principled BSDF')
        bsdf = out.inputs['Surface'].links[0].from_node
        for channel, socket in [('base_color', 'Base Color'), ('roughness', 'Roughness'), ('metallic', 'Metallic')]:
            input = bsdf.inputs[socket]
            value = input.links[0].from_socket if input.is_linked else input.default_value
            root[channel] = save_float(output, channel, bake_socket(obj, material, value, side))
    (Path(output) / 'bake.json').write_text(json.dumps(root, indent=2) + '\n')
    return root


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--material', required=True); p.add_argument('--object', default='Shader_Ball_JL_01')
    p.add_argument('--kind', choices=['mask', 'material'], default='mask'); p.add_argument('--output', type=Path, required=True)
    p.add_argument('--resolution', type=int, choices=[32, 64, 128, 256, 512, 1024, 2048, 4096], default=256)
    a = p.parse_args(sys.argv[sys.argv.index('--')+1:])
    bake_material(bpy.data.objects[a.object], bpy.data.materials[a.material], a.output, a.kind, a.resolution)
