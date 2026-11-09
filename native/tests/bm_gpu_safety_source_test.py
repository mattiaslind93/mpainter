#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Source-only regression checks; BM GPU implementation remains pending."""
from pathlib import Path
import runpy
import unittest

ROOT = Path(__file__).resolve().parents[2]
CHECK = runpy.run_path(str(ROOT / 'scripts/host/verify-gpu-only-paths.py'))['verify_interactive_brush_transport']


def callbacks(stamps=''):
    return 'viewport->stroke_begin = [&] {}; viewport->stroke_stamps = [&] {' + stamps + '}; viewport->stroke_end = [&] {};'


class BrushGpuBoundary(unittest.TestCase):
    def test_current_interactive_source(self):
        CHECK((ROOT / 'native/host/main.cc').read_text())

    def test_known_sync_bridge_and_direct_readback_are_rejected(self):
        for statement in ('host.capture_layer_below(set,id,0,1001,128,rgba);',
                          'GPU_texture_read(image,GPU_DATA_FLOAT,0);',
                          'native_composite_job(set,false,{},0,cancel);'):
            with self.subTest(statement=statement):
                with self.assertRaises(AssertionError):
                    CHECK(callbacks(statement))

    def test_cpu_fallback_inside_nested_capture_lambda_is_rejected(self):
        for method in ('evaluate', 'evaluate_channels', 'composite', 'composite_user', 'composite_displacement'):
            with self.subTest(method=method):
                with self.assertRaises(AssertionError):
                    CHECK(callbacks('auto capture=[&]{for(int y=0;y<res;++y) TextureDocument::' + method + '(set,x,y,tile);};'))

    def test_begin_and_end_callbacks_are_also_covered(self):
        for callback in ('stroke_begin', 'stroke_end'):
            with self.subTest(callback=callback):
                source = callbacks().replace('viewport->' + callback + ' = [&] {}',
                                             'viewport->' + callback + ' = [&] { host.capture_layer_below(set,id,0,1001,128,rgba); }')
                with self.assertRaises(AssertionError):
                    CHECK(source)

    def test_explicit_export_and_diagnostics_outside_brush_are_not_forbidden(self):
        CHECK(callbacks('gpu_pages.enqueue(stamp);') + ' void explicit_export(){GPU_texture_read(image,GPU_DATA_FLOAT,0);}')

    def test_comments_and_shader_strings_do_not_create_false_calls(self):
        CHECK(callbacks('// GPU_texture_read(image);\n const char *note="TextureDocument::evaluate(set)";'))


if __name__ == '__main__':
    unittest.main()
