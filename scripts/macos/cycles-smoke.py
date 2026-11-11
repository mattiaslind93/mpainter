# SPDX-License-Identifier: GPL-3.0-or-later
"""Run only with the headless Blender executable and isolated user directories."""
import json
import os
from pathlib import Path
import bpy

assert bpy.app.background
output = Path(os.environ['MPAINTER_SMOKE_DIR']).resolve()
output.mkdir(parents=True, exist_ok=True)
scene = bpy.context.scene
scene.render.engine = 'CYCLES'
scene.cycles.samples = 4
scene.cycles.use_denoising = False
scene.render.resolution_x = 64
scene.render.resolution_y = 64
scene.render.resolution_percentage = 100
scene.render.threads_mode = 'FIXED'
scene.render.threads = 6
# CPU render is a separate runtime check; the embedded probe proves Metal GPU use.
backend = os.environ.get('MPAINTER_CYCLES_BACKEND', 'CPU')
assert backend in ('CPU', 'METAL')
devices = []
if backend == 'METAL':
    preferences = bpy.context.preferences.addons['cycles'].preferences
    preferences.compute_device_type = 'METAL'
    devices = [d for d in preferences.get_devices_for_type('METAL') if d.type == 'METAL']
    assert devices, 'No Cycles Metal GPU; refusing a CPU fallback in this test'
    for device in preferences.devices:
        device.use = device in devices
scene.cycles.device = 'GPU' if backend == 'METAL' else 'CPU'
scene.render.image_settings.file_format = 'PNG'
scene.render.filepath = str(output / ('cycles-' + backend.lower() + '.png'))
bpy.ops.render.render(write_still=True)
assert Path(scene.render.filepath).stat().st_size > 100
result = {'blender': bpy.app.version_string, 'background': bpy.app.background,
          'engine': 'CYCLES', 'device': backend, 'devices': [d.name for d in devices], 'samples': 4, 'resolution': [64, 64]}
(output / ('cycles-' + backend.lower() + '.json')).write_text(json.dumps(result, indent=2) + '\n')
print('MPAINTER_CYCLES_' + backend + '_PASS', flush=True)
