"""mPainter native preset <-> stock Blender shader groups, bridge schema 1.

Run with blender -b --python ... -- create CATALOG OUTPUT.blend --repo REPO
or open that .blend in background and use ... -- export OUTPUT_DIRECTORY.
No bpy handler, driver, auto-run script, network access or subprocess is installed.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import sys

import bpy
import numpy as np

ROLES = {1: 'World Normal', 2: 'ID', 3: 'AO', 4: 'Curvature', 5: 'Position', 6: 'Thickness'}
NAMES = ['Rust', 'Edge Wear', 'Crease Dirt', 'Moss', 'Ground Dirt', 'Scratches',
         'Leaks & Stains', 'Surface Dust', 'Position height', 'Curvature edges']
SLUGS = ['oxide-flecks', 'exposed-edges', 'crevice-sediment', 'organic-growth',
         'ground-contact', 'surface-scuffs', 'gravity-streaks', 'settled-dust',
         'height-fade', 'curvature-ridge']


def get_path(obj, path):
    for key in path:
        obj = obj[key]
    return obj


def set_path(obj, path, value):
    parent = get_path(obj, path[:-1])
    old = parent[path[-1]]
    if isinstance(old, bool):
        if value not in (0, 1):
            raise ValueError('Boolean controls must be 0 or 1')
        value = bool(value)
    elif path[-1] in ('axis', 'curve', 'generator', 'response', 'map_filter') or (len(path)>1 and path[-2] == 'native_grunge' and path[-1] in (2, 3)):
        if value != int(value):
            raise ValueError('Integer controls must be integral')
        value = int(value)
    if not math.isfinite(float(value)):
        raise ValueError('Non-finite control')
    parent[path[-1]] = value


def raw_image(directory, desc, name):
    # Each image is packed float data, in mPainter's lower-left, linear UV frame.
    pixels = np.fromfile(directory / desc['file'], dtype='<f4')
    if pixels.size != desc['width'] * desc['height'] * 4 or not np.isfinite(pixels).all():
        raise ValueError('Invalid float image payload')
    digest = hashlib.sha256(pixels.tobytes()).hexdigest()
    for image in bpy.data.images:
        if image.get('mp_pixels') == digest:
            return image
    image = bpy.data.images.new(name, width=desc['width'], height=desc['height'], float_buffer=True)
    image.colorspace_settings.name = 'Non-Color'
    image.alpha_mode = 'CHANNEL_PACKED'
    image.pixels.foreach_set(pixels)
    image.update()
    image.pack()
    image['mp_pixels'] = digest
    return image


def socket(tree, name, direction, kind='NodeSocketFloat', default=None):
    item = tree.interface.new_socket(name=name, in_out=direction, socket_type=kind)
    if default is not None:
        item.default_value = default
    return item


class Graph:
    def __init__(self, name):
        self.tree = bpy.data.node_groups.new(name, 'ShaderNodeTree')
        self.tree.use_fake_user = True

    def node(self, kind, label=''):
        n = self.tree.nodes.new(kind)
        n.label = label
        # Regular columns keep the generated arithmetic inspectable.
        i = len(self.tree.nodes) - 1
        n.location = ((i % 8) * 210, -(i // 8) * 180)
        return n

    def wire(self, value, target):
        if isinstance(value, bpy.types.NodeSocket):
            self.tree.links.new(value, target)
        else:
            target.default_value = value

    def value(self, value, label='', path=None):
        n = self.node('ShaderNodeValue', label)
        n.outputs[0].default_value = value
        if path is not None:
            n['mp_path'] = json.dumps(path)
            n['mp_initial'] = float(n.outputs[0].default_value)
            n.use_custom_color = True
            n.color = (0.14, 0.35, 0.28)
        return n.outputs[0]

    def math(self, operation, *values):
        n = self.node('ShaderNodeMath')
        n.operation = operation
        for value, target in zip(values, n.inputs):
            self.wire(value, target)
        return n.outputs[0]

    def add(self, a, b): return self.math('ADD', a, b)
    def sub(self, a, b): return self.math('SUBTRACT', a, b)
    def mul(self, a, b): return self.math('MULTIPLY', a, b)
    def div(self, a, b): return self.math('DIVIDE', a, b)
    def power(self, a, b): return self.math('POWER', a, b)
    def maximum(self, a, b): return self.math('MAXIMUM', a, b)
    def minimum(self, a, b): return self.math('MINIMUM', a, b)
    def clamp(self, a): return self.minimum(1, self.maximum(0, a))
    def lerp(self, a, b, t): return self.add(a, self.mul(self.sub(b, a), t))
    def smooth(self, a): return self.mul(self.mul(a, a), self.sub(3, self.mul(2, a)))

    def xyz(self, value):
        n = self.node('ShaderNodeSeparateXYZ')
        self.wire(value, n.inputs[0])
        return tuple(n.outputs)

    def vector(self, values):
        n = self.node('ShaderNodeCombineXYZ')
        for v, s in zip(values, n.inputs): self.wire(v, s)
        return n.outputs[0]

    def sample(self, image, uv=None, filtering='Linear', repeat=False):
        n = self.node('ShaderNodeTexImage', image.name)
        n.image = image
        n.interpolation = filtering
        n.extension = 'REPEAT' if repeat else 'EXTEND'
        if uv is None:
            uvn = self.node('ShaderNodeUVMap')
            uvn.uv_map = 'UVMap'
            uv = uvn.outputs[0]
        self.wire(uv, n.inputs['Vector'])
        return n.outputs['Color']

    def group(self, tree, previous=None):
        n = self.node('ShaderNodeGroup', tree.name)
        n.node_tree = tree
        if previous is not None:
            self.wire(previous, n.inputs['Previous'])
        return n

    def output(self, name, value, kind='NodeSocketFloat'):
        socket(self.tree, name, 'OUTPUT', kind)
        out = next((n for n in self.tree.nodes if n.type == 'GROUP_OUTPUT'), None)
        if out is None: out = self.node('NodeGroupOutput')
        self.wire(value, out.inputs[name])


def map_group(role, image, filtering):
    name = f'mPainter.Input.{ROLES[role]}.{filtering}'
    old = bpy.data.node_groups.get(name)
    if old:
        # Inputs are shared only when the actual map image matches.
        texture = next(n for n in old.nodes if n.type == 'TEX_IMAGE')
        if texture.image == image: return old
    g = Graph(name)
    uv = g.node('ShaderNodeUVMap'); uv.uv_map = 'UVMap'
    xy = g.xyz(uv.outputs[0])
    # Native masks evaluate at texel centers. Quantizing the map lookup prevents
    # tiny Cycles barycentric errors from changing nearly-zero decoded normals.
    centered = [g.div(g.add(g.math('FLOOR', g.mul(xy[i], image.size[i])), .5), image.size[i]) for i in range(2)]
    g.output('Color', g.sample(image, g.vector((*centered, 0)), filtering=filtering), 'NodeSocketColor')
    g.tree['mp_role'] = role
    return g.tree


def inputs(g, images, filtering='Linear'):
    return {role: g.xyz(g.group(map_group(role, image, filtering)).outputs['Color'])
            for role, image in images.items()}


def select_axis(g, components, axis):
    out = 0
    for i, component in enumerate(components):
        out = g.add(out, g.mul(component, g.math('COMPARE', axis, i, 0.01)))
    return out


def generator(g, e, path, images):
    if any(e.get('input_anchors', [])):
        raise NotImplementedError('Live document anchors require baking')
    maps = inputs(g, images, 'Linear' if e.get('map_filter', 0) else 'Closest')
    control = lambda key: g.value(e[key], key, path + [key])
    weight = lambda i, label: g.value(e['native_grunge'][i], label, path + ['native_grunge', i])
    axis, response, kind = control('axis'), e.get('response', 0), e['generator']
    if kind == 1: v = select_axis(g, maps[5], axis)
    elif kind in (2, 3, 4): v = maps[{2: 4, 3: 3, 4: 6}[kind]][0]
    elif kind == 5:
        v = g.mul(g.sub(1, g.clamp(maps[3][0])), g.sub(1, g.mul(control('dirt_curvature'), g.clamp(maps[4][0]))))
    elif kind == 6:
        normal = [g.sub(g.mul(c, 2), 1) for c in maps[1]]
        light = [g.value(v, f'Light {i}', path + ['light_direction', i]) for i, v in enumerate(e['light_direction'])]
        dot, nsq, lsq = 0, 0, 0
        for n, l in zip(normal, light):
            dot, nsq, lsq = g.add(dot, g.mul(n, l)), g.add(nsq, g.mul(n, n)), g.add(lsq, g.mul(l, l))
        v = g.maximum(0, g.div(dot, g.math('SQRT', g.mul(nsq, lsq))))
        v = g.mul(v, g.mul(g.math('GREATER_THAN', nsq, 1e-12), g.math('GREATER_THAN', lsq, 1e-12)))
    elif kind == 8:
        convex = g.maximum(0, g.mul(g.sub(maps[4][0], .5), 2))
        if response: convex = g.minimum(1, g.mul(g.power(convex, .6), 1.32))
        v = g.mul(g.mul(convex, weight(5, 'Curvature influence')), g.lerp(1, g.clamp(maps[3][0]), weight(4, 'AO influence')))
    elif kind == 9:
        occ = g.sub(1, g.clamp(maps[3][0]))
        conc = g.maximum(0, g.mul(g.sub(.5, maps[4][0]), 2))
        if response: occ, conc = g.power(occ, .72), g.power(conc, .72)
        v = g.clamp(g.add(g.mul(occ, weight(4, 'AO influence')), g.mul(conc, weight(5, 'Curvature influence'))))
    else:
        raise NotImplementedError(f'Generator {kind} needs an explicit native reference')
    if response == 3:
        up = g.clamp(g.add(g.mul(g.sub(g.mul(maps[1][1], 2), 1), .75), .18))
        v = g.clamp(g.add(g.lerp(v, up, weight(6, 'Position influence')), g.mul(.12, up)))
    if kind in (8, 9):
        for role, idx, label in [(5, 6, 'Position influence'), (1, 7, 'Normal influence'), (6, 8, 'Thickness influence')]:
            if response == 3 and role == 5: continue
            w = weight(idx, label)
            if role == 6: factor = g.sub(1, g.clamp(maps[role][0]))
            elif role == 1 and response: factor = g.maximum(0, g.sub(g.mul(select_axis(g, maps[role], axis), 2), 1))
            else: factor = g.clamp(select_axis(g, maps[role], axis) if response else maps[role][2])
            v = g.lerp(v, factor, w) if response == 4 and role == 5 else g.mul(v, g.lerp(1, factor, w))
    support = v
    contrast, bias, invert = control('contrast'), control('bias'), control('invert')
    v = g.sub(v, .5)
    if response == 4: v = g.add(v, bias)
    v = g.add(g.mul(v, contrast), .5)
    if response != 4: v = g.add(v, bias)
    v = g.clamp(v)
    v = g.lerp(v, g.sub(1, v), invert)
    curve = control('curve')
    v = g.lerp(v, g.smooth(v), g.math('COMPARE', curve, 1, .01))
    v = g.lerp(v, g.mul(v, v), g.math('COMPARE', curve, 2, .01))
    if 0 < response < 4 and kind in (8, 9):
        v = g.lerp(invert, v, g.math('GREATER_THAN', support, 0))
    return v


def bitmap_fill(g, e, path, image, images, resolution):
    m, p = e['mapping'], e['mapping']['spatial']
    if m['atom'] or e.get('anchor_source') or p['physical'] or p.get('symmetry'):
        raise NotImplementedError('Atom/anchor/physical/symmetry fill needs baking')
    if p['mode'] == 0 and m['scale'] == [1, 1] and m['offset'] == [0, 0] and m['rotation'] == 0 and not m['use_alpha'] and m['filtering'] == 1 and m['wrap'] in (0, 1):
        return g.xyz(g.sample(image, filtering='Closest', repeat=m['wrap'] == 0))[0]
    if p['mode'] != 1 or any(p['rotation3']) or any(p['offset3']) or m['scale'] != [1, 1] or m['offset'] != [0, 0] or m['rotation'] != 0:
        raise NotImplementedError('This editable bitmap subset requires unrotated triplanar projection')
    if m['use_alpha']:
        raise NotImplementedError('Alpha-weighted bitmap fill needs baking')
    maps = inputs(g, images, 'Closest')  # native surface atlas has point sampling
    position = [g.sub(g.mul(c, 2), 1) for c in maps[5]]
    normal = [g.sub(g.mul(c, 2), 1) for c in maps[1]]
    scale = [g.value(v, f'Scale {i}', path + ['mapping', 'spatial', 'scale3', i]) for i, v in enumerate(p['scale3'])]
    hardness = g.value(p['hardness'], 'Hardness', path + ['mapping', 'spatial', 'hardness'])
    exponent = g.add(1, g.mul(hardness, 15))
    weights = [g.power(g.math('ABSOLUTE', g.mul(n, s)), exponent) for n, s in zip(normal, scale)]
    total = g.add(g.add(weights[0], weights[1]), weights[2])
    v = 0
    # Exactly the native four footprint taps for each of the three projections.
    for axis in range(3):
        x, y = (1 if axis == 0 else 0), (1 if axis == 2 else 2)
        plane = 0
        for dx in (-.25, .25):
            for dy in (-.25, .25):
                u = g.add(g.div(g.add(position[x], dx * 2 / resolution), g.mul(scale[x], 2)), .5)
                w = g.add(g.div(g.add(position[y], dy * 2 / resolution), g.mul(scale[y], 2)), .5)
                color = g.sample(image, g.vector((u, w, 0)), repeat=True)
                plane = g.add(plane, g.xyz(color)[0])
        v = g.add(v, g.mul(g.div(weights[axis], total), g.mul(plane, .25)))
    return g.mul(v, g.math('GREATER_THAN', total, 1e-20))


def levels(g, e, path, previous):
    c = lambda key: g.value(e['levels'][key], key, path + ['levels', key])
    low, high, gamma = c('input_low'), c('input_high'), c('gamma')
    v = g.div(g.sub(previous, low), g.sub(high, low))
    v = g.lerp(v, g.power(g.maximum(0, v), g.div(1, gamma)), g.math('GREATER_THAN', v, 0))
    outlow, outhigh = c('output_low'), c('output_high')
    v = g.add(outlow, g.mul(g.sub(outhigh, outlow), v))
    return g.clamp(v) if e['levels']['clamp'] else v


def blend(g, source, previous, mode, opacity):
    if mode in ('Normal', 'Replace'): v = source
    elif mode == 'Multiply': v = g.mul(source, previous)
    elif mode == 'Linear Dodge': v = g.add(source, previous)
    elif mode == 'Subtract': v = g.sub(previous, source)
    elif mode == 'Screen': v = g.sub(1, g.mul(g.sub(1, source), g.sub(1, previous)))
    elif mode == 'Darken': v = g.minimum(source, previous)
    elif mode == 'Lighten': v = g.maximum(source, previous)
    elif mode == 'Disable': return previous
    else: raise NotImplementedError(f'Blend mode {mode} needs baking')
    return g.lerp(previous, v, opacity)


def make_mask(mask, directory, images, name, path, resolution):
    if mask.get('has_paint'):
        raise NotImplementedError('Sparse mask paint needs a reference snapshot')
    g = Graph(name)
    previous = g.value(mask['background'], 'Background', path + ['background'])
    for i, e in enumerate(mask['effects']):
        ep = path + ['effects', i]
        eg = Graph(name + ' / ' + e.get('display_name', e['type']))
        socket(eg.tree, 'Previous', 'INPUT')
        previous_in = eg.node('NodeGroupInput').outputs['Previous']
        if not e['enabled']:
            eg.tree['mp_note'] = 'Disabled native effect; retained unchanged'
            output = previous_in
        else:
            if e['id'] in mask['fallbacks']:
                image = raw_image(directory, mask['fallbacks'][e['id']], 'mPainter.NativeReference.' + e['id'])
                value = eg.xyz(eg.sample(image, filtering='Closest'))[0]
                eg.tree['mp_note'] = 'BAKED REFERENCE: native hash noise / neighborhood operation, fixed to JL01 and export resolution. Internal parameters locked; surrounding stack remains editable.'
            elif e['type'] == 'generator': value = generator(eg, e, ep, images)
            elif e['type'] == 'levels': value = levels(eg, e, ep, previous_in)
            elif e['type'] == 'fill':
                if e['id'] in mask['textures']:
                    image = raw_image(directory, mask['textures'][e['id']], 'mPainter.Grunge.' + e['id'])
                    value = bitmap_fill(eg, e, ep, image, images, resolution)
                elif not e.get('anchor_source') and not e['mapping']['atom'] and e['mapping']['spatial']['mode'] in (0, 1):
                    value = eg.value(e['value'], 'Value', ep + ['value'])
                else: raise NotImplementedError('Unresolved fill requires baking')
            else: raise NotImplementedError(f"Effect {e['type']} is preserved in the archive but requires baking for a shader graph")
            if e['type'] == 'levels':
                mode = e['levels'].get('blending', 'Normal')
                opacity = eg.value(e['levels'].get('opacity', 1), 'Opacity')
            else:
                mode = e.get('blending', 'Normal')
                opacity = eg.value(e.get('opacity', 1), 'Opacity', ep + ['opacity'])
            output = blend(eg, value, previous_in, mode, opacity)
        eg.output('Mask', output)
        previous = g.group(eg.tree, previous).outputs['Mask']
    g.output('Mask', g.clamp(previous))
    return g.tree


def mix_color(g, below, above, factor):
    n = g.node('ShaderNodeMixRGB')
    n.blend_type = 'MIX'
    g.wire(factor, n.inputs[0]); g.wire(below, n.inputs[1]); g.wire(above, n.inputs[2])
    return n.outputs[0]


def make_material(data, directory, images, name):
    g = Graph(name)
    color, rough, metal = (0, 0, 0, 1), 0., 0.
    layers = data['layers']
    for i, layer in enumerate(layers):
        if layer['kind'] == 'folder':
            if i != len(layers) - 1 or layer.get('mask') or layer['opacity'] != [1]*5 or not layer['visible']:
                raise NotImplementedError('Only a single unmasked root folder is editable; bake other hierarchies')
            continue
        if layer['kind'] != 'fill' or layer['content_effects'] or not layer['visible']:
            raise NotImplementedError('Editable material nodes currently support visible uniform Fill layers; bake others')
        if layer['textured'] and (layer.get('projection_mode') != 6 or layer.get('projection_filtering') != 2):
            raise NotImplementedError('Textured materials require Match per UV Tile / Nearest, or explicit baking')
        if any(layer['blending'][j] != 'Normal' for j in (0, 1, 2)):
            raise NotImplementedError('Material blending outside Normal needs baking')
        if layer['values']['base_color'][3] != 1:
            raise NotImplementedError('Translucent material layers need baking')
        prefix = ['layers', i]
        v = layer['values']
        channels = [g.value(v['base_color'][j], layer['name'] + f' Color {j}', prefix + ['values', 'base_color', j]) for j in range(3)]
        rgb = g.vector(channels)
        r = g.value(v['roughness'], layer['name'] + ' Roughness', prefix + ['values', 'roughness'])
        m = g.value(v['metallic'], layer['name'] + ' Metallic', prefix + ['values', 'metallic'])
        for channel, desc in layer.get('channel_textures', {}).items():
            image = raw_image(directory, desc, layer['name'] + ' channel ' + channel)
            rgba = np.empty(len(image.pixels), np.float32); image.pixels.foreach_get(rgba)
            if np.any(rgba[3::4] != 1):
                raise NotImplementedError('Material texture alpha needs a native reference snapshot')
            sampled = g.sample(image, filtering='Closest')
            if channel == '0': rgb = sampled
            elif channel == '1': m = g.xyz(sampled)[0]
            elif channel == '2': r = g.xyz(sampled)[0]
        fac = 1
        if layer.get('mask') and layer['mask']['enabled']:
            mask = make_mask(layer['mask'], directory, images, name + ' / ' + layer['name'] + ' Mask', prefix + ['mask'], data['resolution'])
            fac = g.group(mask).outputs['Mask']
        if layer['channels'][0]: color = mix_color(g, color, rgb, g.mul(fac, layer['opacity'][0]))
        if layer['channels'][2]: rough = g.lerp(rough, r, g.mul(fac, layer['opacity'][2]))
        if layer['channels'][1]: metal = g.lerp(metal, m, g.mul(fac, layer['opacity'][1]))
    g.output('Base Color', color, 'NodeSocketColor')
    g.output('Roughness', rough)
    g.output('Metallic', metal)
    return g.tree


def walk(tree, visited=None):
    visited = set() if visited is None else visited
    if tree.name in visited: return
    visited.add(tree.name)
    yield tree
    for node in tree.nodes:
        if node.type == 'GROUP': yield from walk(node.node_tree, visited)


def signature(root):
    """Accept only bound Value edits; reject rewiring/changed operations/images/defaults."""
    records = []
    def simple(value):
        if isinstance(value, (str, int, float, bool)): return value
        return list(value)
    for tree in walk(root):
        nodes = []
        for n in tree.nodes:
            entry = {'name': n.name, 'type': n.bl_idname, 'mute': n.mute,
                     'binding': n.get('mp_path'), 'initial': n.get('mp_initial'), 'inputs': []}
            for s in n.inputs:
                if hasattr(s, 'default_value'): entry['inputs'].append((s.identifier, simple(s.default_value)))
            if n.type == 'VALUE' and 'mp_path' not in n: entry['value'] = n.outputs[0].default_value
            for prop in ('operation', 'blend_type', 'use_clamp', 'interpolation', 'extension', 'uv_map'):
                if hasattr(n, prop): entry[prop] = getattr(n, prop)
            if n.type == 'GROUP': entry['group'] = n.node_tree.name
            if n.type == 'TEX_IMAGE':
                pixels = np.empty(len(n.image.pixels), np.float32); n.image.pixels.foreach_get(pixels)
                entry['image'] = [n.image.name, hashlib.sha256(pixels.tobytes()).hexdigest(), n.image.colorspace_settings.name, n.image.alpha_mode]
            nodes.append(entry)
        links = sorted((l.from_node.name, l.from_socket.identifier, l.to_node.name, l.to_socket.identifier) for l in tree.links)
        interface = [(s.name, s.in_out, s.socket_type) for s in tree.interface.items_tree if s.item_type == 'SOCKET']
        records.append((tree.name, nodes, links, interface))
    return hashlib.sha256(json.dumps(records, sort_keys=True).encode()).hexdigest()


def import_package(path, display_name=None):
    path = Path(path)
    data = json.loads(path.read_text())
    if data['bridge_version'] != 1: raise ValueError('Unsupported bridge version')
    images = {int(role): raw_image(path.parent, desc, f'shaderball-jl01-map-{role}.png') for role, desc in data['maps'].items()}
    name = 'mPainter.' + (display_name or data['name'])
    existing = set(bpy.data.node_groups)
    try:
        if data['kind'] == 'mask':
            root = make_mask(data['mask'], path.parent, images, name, ['mask'], data['resolution'])
        else: root = make_material(data, path.parent, images, name)
    except NotImplementedError as error:
        # Keep the archive losslessly; clearly label the fixed-surface preview.
        # Unsupported operations never turn into unrelated editable math nodes.
        for tree in list(bpy.data.node_groups):
            if tree not in existing: bpy.data.node_groups.remove(tree, do_unlink=True)
        g = Graph(name)
        references = [('Mask', data['mask']['reference'])] if data['kind'] == 'mask' else [
            ('Base Color', data['reference_color']), ('Roughness', data['reference_roughness']), ('Metallic', data['reference_metallic'])]
        for label, desc in references:
            image = raw_image(path.parent, desc, name + ' REFERENCE ' + label)
            sampled = g.sample(image, filtering='Closest')
            g.output(label, sampled if label == 'Base Color' else g.xyz(sampled)[0],
                     'NodeSocketColor' if label == 'Base Color' else 'NodeSocketFloat')
        root = g.tree
        root['mp_note'] = 'BAKED WHOLE PRESET REFERENCE, JL01 only; archive retained unchanged. ' + str(error)
        print(root['mp_note'], flush=True)
    text = bpy.data.texts.new(root.name + '.preset.json')
    text.write(json.dumps(data))
    root['mp_manifest'] = text.name
    root['mp_signature'] = signature(root)
    root['mp_kind'] = data['kind']
    root['mp_slug'] = data['name']
    root.asset_mark()
    return root


def export_package(root, output):
    for tree in walk(root):
        for node in tree.nodes:
            if node.type == 'TEX_IMAGE' and node.projection != 'FLAT':
                raise ValueError('Changed image projection requires explicit bake export')
            if node.type == 'UVMAP' and node.from_instancer:
                raise ValueError('Instancer UVs require explicit bake export')
    if signature(root) != root['mp_signature']:
        raise ValueError(f'{root.name}: graph or source pixels changed outside editable controls; use explicit bake export')
    data = json.loads(bpy.data.texts[root['mp_manifest']].as_string())
    for tree in walk(root):
        for node in tree.nodes:
            if 'mp_path' in node:
                if node.outputs[0].default_value != node['mp_initial']:
                    set_path(data, json.loads(node['mp_path']), node.outputs[0].default_value)
    path = Path(output)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, indent=2) + '\n')
    return data


def material_for(root, name=None):
    mat = bpy.data.materials.new(name or root.name)
    mat.use_nodes = True
    nodes, links = mat.node_tree.nodes, mat.node_tree.links
    nodes.clear()
    group = nodes.new('ShaderNodeGroup'); group.node_tree = root; group.location = (-300, 100)
    out = nodes.new('ShaderNodeOutputMaterial'); out.location = (300, 100)
    if root['mp_kind'] == 'mask':
        shader = nodes.new('ShaderNodeEmission'); shader.location = (0, 100)
        links.new(group.outputs['Mask'], shader.inputs['Color'])
    else:
        shader = nodes.new('ShaderNodeBsdfPrincipled'); shader.location = (0, 100)
        for name in ('Base Color', 'Roughness', 'Metallic'): links.new(group.outputs[name], shader.inputs[name])
    links.new(shader.outputs[0], out.inputs['Surface'])
    return mat


def import_obj(path):
    # No implicit axis conversion, triangulation, UV repacking or vertex welding.
    vertices, uvs, faces, face_uvs, ids = [], [], [], [], []
    current, materials = 0, {}
    for line in Path(path).read_text().splitlines():
        p = line.split()
        if not p: continue
        if p[0] == 'v': vertices.append(tuple(map(float, p[1:4])))
        elif p[0] == 'vt': uvs.append(tuple(map(float, p[1:3])))
        elif p[0] == 'usemtl': current = materials.setdefault(p[1], len(materials))
        elif p[0] == 'f':
            indices = [part.split('/') for part in p[1:]]
            faces.append([int(v[0])-1 for v in indices]); face_uvs.append([int(v[1])-1 for v in indices]); ids.append(current)
    mesh = bpy.data.meshes.new('Shader_Ball_JL_01')
    mesh.from_pydata(vertices, [], faces)
    uv = mesh.uv_layers.new(name='UVMap')
    for poly, indices in zip(mesh.polygons, face_uvs):
        poly.use_smooth = True
        for loop, index in zip(poly.loop_indices, indices): uv.data[loop].uv = uvs[index]
    obj = bpy.data.objects.new('Shader_Ball_JL_01', mesh)
    bpy.context.collection.objects.link(obj)
    obj['mp_source_sha256'] = hashlib.sha256(Path(path).read_bytes()).hexdigest()
    obj['mp_uv_policy'] = 'Original OBJ loop UVs, no unwrap or repack'
    return obj, ids


def create_base(catalog, output, repo):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.context.scene.render.threads_mode = 'FIXED'; bpy.context.scene.render.threads = 2
    roots = [import_package(catalog / slug / 'preset.json', name) for slug, name in zip(SLUGS, NAMES)]
    roots.append(import_package(catalog / 'rust-material/preset.json', 'Rust Material'))
    obj, ids = import_obj(repo / 'assets/meshes/Shader_Ball_JL_01.obj')
    for root in roots: obj.data.materials.append(material_for(root))
    # The supplied OBJ has one material assignment. Its ID bake is therefore
    # uniformly red on covered UVs; uncovered pixels remain zero.
    image = bpy.data.images.new('shaderball-jl01-map-2.png', width=256, height=256, float_buffer=True)
    image.colorspace_settings.name = 'Non-Color'; image.alpha_mode = 'CHANNEL_PACKED'
    pixels = np.zeros((256, 256, 4), dtype=np.float32)
    uv = obj.data.uv_layers.active.data
    obj.data.calc_loop_triangles()
    for tri in obj.data.loop_triangles:
        points = np.array([uv[i].uv[:] for i in tri.loops]) * 256
        lo = np.maximum(0, np.floor(points.min(axis=0)).astype(int)); hi = np.minimum(255, np.ceil(points.max(axis=0)).astype(int))
        a, b, c = points
        den = (b[1]-c[1])*(a[0]-c[0])+(c[0]-b[0])*(a[1]-c[1])
        if abs(den) < 1e-12: continue
        for y in range(lo[1], hi[1]+1):
            for x in range(lo[0], hi[0]+1):
                p = np.array([x+.5, y+.5])
                s = ((b[1]-c[1])*(p[0]-c[0])+(c[0]-b[0])*(p[1]-c[1]))/den
                t = ((c[1]-a[1])*(p[0]-c[0])+(a[0]-c[0])*(p[1]-c[1]))/den
                if s >= 0 and t >= 0 and s+t <= 1: pixels[y, x] = (1, 0, 0, 1)
    if len(set(ids)) != 1: raise ValueError('ID rasterizer expects the bundled single-material mesh')
    image.pixels.foreach_set(pixels.ravel()); image.update(); image.pack()
    image['mp_note'] = 'Material ID rasterized from the original OBJ: one red ID, alpha is UV coverage'
    id_group = map_group(2, image, 'Closest'); id_group.asset_mark()
    # An explicit input board makes all six maps easy to find in Shader Editor.
    board = Graph('mPainter.Mesh Map Inputs')
    for role in (4, 3, 6, 5, 1, 2):
        tree = id_group if role == 2 else next(t for t in bpy.data.node_groups if t.get('mp_role') == role)
        board.output(ROLES[role], board.group(tree).outputs['Color'], 'NodeSocketColor')
    board.tree.asset_mark()
    node = obj.data.materials[0].node_tree.nodes.new('ShaderNodeGroup'); node.node_tree = board.tree; node.location = (-650, -200)
    bpy.context.view_layer.objects.active = obj; obj.select_set(True)
    note = bpy.data.texts.new('README - mPainter Blender Masks')
    note.write('Editable mPainter masks and Material Presets. Green Value nodes are exportable controls.\n'
               'Use scripts/blender/mpainter_presets.py to export JSON, then bts-blender-presets import to create native archives.\n'
               'Unknown graph edits are rejected; arbitrary graphs need explicit baking.\n'
               'Leaks & Stains has a labeled native reference for custom integer-hash noise.\n'
               'The five shipped mesh maps are downsampled to 256 using mPainter itself. ID is rasterized from the OBJ.\n\n')
    note.write((repo / 'assets/meshes/Shader_Ball_JL_01-NOTICE.md').read_text())
    bpy.context.scene['mp_bridge_version'] = 1
    bpy.context.scene.view_settings.view_transform = 'Standard'
    bpy.context.scene.view_settings.look = 'None'
    bpy.ops.wm.save_as_mainfile(filepath=str(output))
    return roots


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='command', required=True)
    p = sub.add_parser('create'); p.add_argument('catalog', type=Path); p.add_argument('output', type=Path); p.add_argument('--repo', type=Path, required=True)
    p = sub.add_parser('export'); p.add_argument('output', type=Path)
    p = sub.add_parser('import'); p.add_argument('package', type=Path); p.add_argument('output', type=Path)
    args = parser.parse_args(sys.argv[sys.argv.index('--')+1:])
    if args.command == 'create': create_base(args.catalog, args.output, args.repo)
    elif args.command == 'export':
        for root in bpy.data.node_groups:
            if 'mp_manifest' in root: export_package(root, args.output / (root['mp_slug'] + '.json'))
    else:
        import_package(args.package)
        bpy.ops.wm.save_as_mainfile(filepath=str(args.output))


if __name__ == '__main__': main()
