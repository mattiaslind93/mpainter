# SPDX-License-Identifier: GPL-3.0-or-later
"""Blender background fixture: run with --python this.py -- /path/material.blend."""
import sys
import bpy
from pathlib import Path
bpy.ops.wm.read_factory_settings(use_empty=True)
m = bpy.data.materials.new('Brand Test - Turquoise Copper Noise')
m.use_nodes = True
m.use_fake_user = True
n = m.node_tree.nodes
n.clear()
out = n.new('ShaderNodeOutputMaterial')
bsdf = n.new('ShaderNodeBsdfPrincipled')
bsdf.inputs['Metallic'].default_value = 0.35
bsdf.inputs['Roughness'].default_value = 0.32
uv = n.new('ShaderNodeTexCoord')
noise = n.new('ShaderNodeTexNoise')
noise.inputs['Scale'].default_value = 7.0
noise.inputs['Detail'].default_value = 2.0
ramp = n.new('ShaderNodeValToRGB')
ramp.color_ramp.elements[0].position = 0.35
ramp.color_ramp.elements[0].color = (0.008, 0.32, 0.39, 1)
ramp.color_ramp.elements[1].position = 0.65
ramp.color_ramp.elements[1].color = (0.8, 0.18, 0.035, 1)
l = m.node_tree.links
l.new(uv.outputs['UV'], noise.inputs['Vector'])
l.new(noise.outputs['Fac'], ramp.inputs['Fac'])
l.new(ramp.outputs['Color'], bsdf.inputs['Base Color'])
l.new(bsdf.outputs['BSDF'], out.inputs['Surface'])
m.asset_mark()
bpy.ops.wm.save_as_mainfile(filepath=str(Path(sys.argv[sys.argv.index('--') + 1]).absolute()))
print('BRAND_FIXTURE_OK', m.name, len(n), len(l))
