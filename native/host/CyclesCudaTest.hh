#pragma once
// Runs in the embedded Blender process, with no Qt/OpenGL viewport. CUDA is
// mandatory: missing devices, kernel/JIT errors and blank output all fail.
namespace bts {
inline constexpr const char *cycles_cuda_test_python = R"PY(
import bpy, cycles, _cycles, os, time, json
from pathlib import Path
out = Path(output_directory)
out.mkdir(parents=True, exist_ok=True)
result = {'pass': False, 'binary': os.readlink('/proc/self/exe'),
          'blender': bpy.app.version_string, 'cycles_module': cycles.__file__}
try:
    _cycles.init(os.path.dirname(cycles.__file__), bpy.utils.user_resource('CONFIG'), True)
    p = bpy.context.preferences.addons['cycles'].preferences
    p.compute_device_type = 'CUDA'
    devices = [d for d in p.get_devices_for_type('CUDA') if d.type == 'CUDA']
    assert devices, 'No CUDA GPU detected; CPU fallback is forbidden in this test'
    for d in p.devices:
        d.use = d in devices
    _cycles.set_device_override('CUDA')
    s = bpy.context.scene
    s.render.engine = 'CYCLES'
    s.cycles.device = 'GPU'
    s.cycles.samples = 8
    s.cycles.use_denoising = False
    s.render.threads_mode = 'FIXED'
    s.render.threads = 2
    s.render.resolution_x = s.render.resolution_y = 64
    s.render.resolution_percentage = 100
    s.render.image_settings.file_format = 'PNG'
    s.render.filepath = str(out / 'cycles-cuda.png')
    assert s.camera is not None, 'Factory scene camera missing'
    start = time.monotonic()
    bpy.ops.render.render(write_still=True)
    elapsed = time.monotonic() - start
    im = bpy.data.images.load(s.render.filepath, check_existing=False)
    pix = list(im.pixels)
    assert tuple(im.size) == (64, 64) and max(pix[0::4]) - min(pix[0::4]) > 0.01, 'Blank render'
    result.update({'pass': True, 'device': s.cycles.device, 'backend': 'CUDA',
                   'devices': [d.name for d in devices], 'samples': 8,
                   'render_seconds': elapsed, 'image_bytes': Path(s.render.filepath).stat().st_size})
except Exception as exc:
    result['error'] = str(exc)
    raise
finally:
    (out / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
    print('BTS_CYCLES_CUDA_TEST ' + json.dumps(result), flush=True)
)PY";
}
