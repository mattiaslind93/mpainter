"""Run after opening the generated base .blend with blender -b (CPU, two threads)."""
import argparse
import json
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
import bpy
import numpy as np
import mpainter_presets as bridge
from preset_bake import bake_socket, bake_material, save_float


def plane():
    mesh = bpy.data.meshes.new('UV evaluation plane')
    mesh.from_pydata([(-1,-1,0),(1,-1,0),(1,1,0),(-1,1,0)], [], [(0,1,2,3)])
    uv = mesh.uv_layers.new(name='UVMap')
    for loop, value in zip(uv.data, [(0,0),(1,0),(1,1),(0,1)]): loop.uv = value
    obj = bpy.data.objects.new('UV evaluation plane', mesh); bpy.context.collection.objects.link(obj)
    return obj


def run(catalog, output):
    output.mkdir(parents=True, exist_ok=True)
    bpy.context.scene.render.threads_mode = 'FIXED'; bpy.context.scene.render.threads = 2
    obj = plane()
    original = bpy.data.objects['Shader_Ball_JL_01']; original.hide_render = True
    roots = sorted([t for t in bpy.data.node_groups if 'mp_manifest' in t], key=lambda t:t['mp_slug'])
    assert len(roots) == 11
    records = []
    for root in roots:
        slug = root['mp_slug']
        directory = output / slug; directory.mkdir(exist_ok=True)
        native = json.loads((catalog / slug / 'preset.json').read_text())
        bridge.export_package(root, directory / 'unchanged.json')
        mat = bridge.material_for(root, 'Validation ' + slug)
        group = next(n for n in mat.node_tree.nodes if n.type == 'GROUP')
        channels = [('Mask', native['mask']['reference'])] if root['mp_kind'] == 'mask' else [
            ('Base Color', native['reference_color']), ('Roughness', native['reference_roughness']), ('Metallic', native['reference_metallic'])]
        for name, reference in channels:
            result = bake_socket(obj, mat, group.outputs[name])
            key = name.lower().replace(' ', '-')
            save_float(directory, 'blender-' + key, result)
            expected = np.fromfile(catalog / slug / reference['file'], dtype='<f4').reshape(256,256,4)
            delta = np.abs(result[...,:3] - expected[...,:3])
            record = {'preset': slug, 'channel': name, 'mean_absolute_error': float(delta.mean()), 'maximum_absolute_error': float(delta.max()),
                      'rms_error': float(np.sqrt((delta*delta).mean())), 'pixels': 256*256}
            records.append(record); print(json.dumps(record), flush=True)
        # Demonstrate that actual live controls, not retained JSON alone, export.
        control = next(n for tree in bridge.walk(root) for n in tree.nodes if 'mp_path' in n and
                       json.loads(n['mp_path'])[-1] == ('contrast' if root['mp_kind'] == 'mask' else 'roughness'))
        before = control.outputs[0].default_value
        control.outputs[0].default_value = min(7, before + .4) if root['mp_kind'] == 'mask' else .61
        data = bridge.export_package(root, directory / 'edited.json')
        assert bridge.get_path(data, json.loads(control['mp_path'])) != bridge.get_path(native, json.loads(control['mp_path']))
        name = 'Mask' if root['mp_kind'] == 'mask' else 'Roughness'
        edited = bake_socket(obj, mat, group.outputs[name]); save_float(directory, 'edited-blender', edited)
        control.outputs[0].default_value = before
        # Graph changes must fail rather than produce an unrelated native preset.
        mathnode = next(n for tree in bridge.walk(root) for n in tree.nodes if n.type == 'MATH')
        operation = mathnode.operation; mathnode.operation = 'SINE' if operation != 'SINE' else 'COSINE'
        try:
            bridge.export_package(root, directory / 'must-not-exist.json')
        except ValueError: pass
        else: raise AssertionError('Changed node operation was silently discarded')
        finally: mathnode.operation = operation
        image_node = next(n for tree in bridge.walk(root) for n in tree.nodes if n.type == 'TEX_IMAGE')
        image_node.projection = 'BOX'
        try:
            bridge.export_package(root, directory / 'must-not-exist.json')
        except ValueError: pass
        else: raise AssertionError('Changed image projection was silently discarded')
        finally: image_node.projection = 'FLAT'
        bpy.data.materials.remove(mat)
    # Arbitrary Blender-authored graph: UV -> sine -> multiply/add -> emission.
    mat = bpy.data.materials.new('Arbitrary Blender mask'); mat.use_nodes = True
    n, l = mat.node_tree.nodes, mat.node_tree.links; n.clear()
    tex=n.new('ShaderNodeTexCoord'); sep=n.new('ShaderNodeSeparateXYZ'); l.new(tex.outputs['UV'],sep.inputs[0])
    mul=n.new('ShaderNodeMath'); mul.operation='MULTIPLY'; mul.inputs[1].default_value=31; l.new(sep.outputs['X'],mul.inputs[0])
    sin=n.new('ShaderNodeMath'); sin.operation='SINE'; l.new(mul.outputs[0],sin.inputs[0])
    half=n.new('ShaderNodeMath'); half.operation='MULTIPLY_ADD'; half.inputs[1].default_value=.5; half.inputs[2].default_value=.5; l.new(sin.outputs[0],half.inputs[0])
    emit=n.new('ShaderNodeEmission'); l.new(half.outputs[0],emit.inputs['Color'])
    out=n.new('ShaderNodeOutputMaterial'); l.new(emit.outputs[0],out.inputs['Surface'])
    bake_material(obj, mat, output/'arbitrary-mask', 'mask')
    # Arbitrary Principled material with procedural color and linked roughness.
    bsdf=n.new('ShaderNodeBsdfPrincipled'); l.new(half.outputs[0],bsdf.inputs['Base Color']); l.new(half.outputs[0],bsdf.inputs['Roughness']); bsdf.inputs['Metallic'].default_value=.37
    l.new(bsdf.outputs[0],out.inputs['Surface'])
    bake_material(obj, mat, output/'arbitrary-material', 'material')
    (output/'blender-results.json').write_text(json.dumps({'blender': bpy.app.version_string, 'records': records,
        'controls_edited': len(roots), 'unsupported_graph_edits_rejected': len(roots),
        'image_projection_edits_rejected': len(roots), 'arbitrary_graph_bakes': 2},indent=2)+'\n')
    original.hide_render = False
    print('Blender graph evaluation and export complete',flush=True)


if __name__ == '__main__':
    p=argparse.ArgumentParser();p.add_argument('catalog',type=Path);p.add_argument('output',type=Path)
    a=p.parse_args(sys.argv[sys.argv.index('--')+1:]);run(a.catalog,a.output)
