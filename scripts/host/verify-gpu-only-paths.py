#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Fail closed when the CPU composite worker escapes explicit export/oracles."""
from pathlib import Path
import re
root = Path(__file__).resolve().parents[2]
host = root / 'native/host'
def masked(source):
    # Keep offsets while hiding comments, normal strings and shader raw strings.
    pattern = r'R"([^ ()\\\t\r\n]*)\([\s\S]*?\)\1"|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|//[^\n]*|/\*[\s\S]*?\*/'
    return re.sub(pattern, lambda m: ''.join('\n' if c == '\n' else ' ' for c in m[0]), source)
def body(code, start):
    first = code.index('{', start)
    depth = 1
    for end in range(first + 1, len(code)):
        depth += (code[end] == '{') - (code[end] == '}')
        if depth == 0:
            return first, end
    raise AssertionError('Unbalanced C++ block')
def verify_interactive_brush_transport(source, label='main.cc'):
    """Brush callbacks cannot acquire CPU copies of an underlying GPU prefix.

    This is a source regression gate, not runtime/GPU verification. Explicit
    export/readback diagnostics elsewhere retain their existing allowlist.
    """
    code = masked(source)
    forbidden = r'\b(?:capture_layer_below|GPU_texture_read|native_composite_job)\s*\(|\bTextureDocument\s*::\s*(?:evaluate(?:_channels)?|composite(?:_[A-Za-z0-9_]+)?)\s*\('
    for callback in ('viewport->stroke_begin', 'viewport->stroke_stamps', 'viewport->stroke_end'):
        start = code.index(callback)
        a, b = body(code, start)
        call = re.search(forbidden, code[a:b])
        assert call is None, f'Synchronous readback/CPU composite in interactive brush callback: {label}:{source.count(chr(10), 0, a + call.start()) + 1}'

file_allowlist = {'NativeCompositeJob.cc', 'NativeCompositeJob.hh', 'TextureExport.cc', 'MeshExport.cc'}
function_allowlist = {
    # Read-back diagnostics, a section MaterialInspection.cc includes.
    'MaterialInspectionDiagnostics.inc': ['MaterialInspection::copy_pixels(', 'MaterialInspection::verify_gpu_pixels('],
    # The GPU stack oracle; GpuLayerPreview.cc includes it from the harness.
    'GpuLayerPreviewSelfTest.inc': ['GpuLayerPreview::self_test('],
}
calls = 0
for path in [*host.rglob('*'), *(root / 'native/harness').rglob('*')]:
    if path.suffix not in ('.cc', '.hh', '.inc'):
        continue
    source = path.read_text()
    code = masked(source)
    allowed = []
    for name in function_allowlist.get(path.name, []):
        allowed.append(body(code, code.index(name)))
    if path.name == 'GpuLayerPreview.cc':
        # Whitespace-tolerant: the guard must hold however the file is formatted.
        guard = re.search(r'if\s*\(\s*!\s*output_tile\s*&&\s*std::getenv\s*\(\s*"BTS_VERIFY_GPU_STACK"\s*\)'
                          r'\s*&&\s*testing_stack', source)
        assert guard, 'GpuLayerPreview.cc: the BTS_VERIFY_GPU_STACK self-test guard is missing'
        allowed.append(body(code, guard.start()))
    for call in re.finditer(r'\bnative_composite_job\s*\(', code):
        calls += 1
        assert path.name in file_allowlist or path.name.endswith('CpuDemo.cc') or any(a < call.start() < b for a, b in allowed), f'CPU composite outside explicit export/diagnostic: {path.name}:{source.count(chr(10), 0, call.start()) + 1}'
import sys
sys.path.insert(0, str(root / 'scripts/host'))
from main_source import compact, main_source, with_sections
verify_interactive_brush_transport(main_source(root))
# Persistence is no exception to the GPU rule.
code = masked((host / 'BlenderHost.cc').read_text())
for name in ('BlenderHost::begin_save_project(', 'BlenderHost::poll_save_project(', 'BlenderHost::save_project(', 'BlenderHost::apply_texture_set('):
    a, b = body(code, code.index(name))
    assert 'queue_composite(' not in code[a:b] and 'TextureDocument::composite(' not in code[a:b]
assert 'reason=save-cache' not in (host / 'BlenderHost.cc').read_text()
assert 'GpuPersistenceScope gpu_only' in (host / 'ProjectIO.cc').read_text(), 'Save worker must reject unhandled CPU authoring recipes'
assert 'if(cpu_paint_evaluation_forbidden)throw' in compact((host / 'PaintPixels.cc').read_text())
# MaterialInspection.cc keeps its code in .inc sections it includes in place.
inspection = masked(with_sections(root, 'native/host/MaterialInspection.cc'))
a, b = body(inspection, inspection.index('MaterialInspection::update_pixels('))
assert 'composite_pixel' not in inspection[a:b] and 'padding->apply(' not in inspection[a:b], 'Native image allocation must not contain a CPU compositor/padding fallback'
a, b = body(inspection, inspection.index('MaterialInspection::preview_uniform('))
assert 'TextureDocument::composite(' not in inspection[a:b] and 'BKE_image_acquire_ibuf(' not in inspection[a:b], 'Uniform controls must use the GPU stack'
for name in ('ViewportResidency.cc','FillTextureIO.cc'):
    assert 'QSettings' not in masked((host / name).read_text()), f'Hot viewport/import path must not wait on settings writers: {name}'
print(f'GPU-only interactive and save paths: PASS ({calls} CPU calls confined to export/diagnostic allowlist)')
