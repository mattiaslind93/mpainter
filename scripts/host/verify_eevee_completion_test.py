"""Small value-only verifier tests; no native app or renderer is started."""
import copy
import importlib.util
from pathlib import Path
import unittest

_spec = importlib.util.spec_from_file_location('eevee_completion_verifier',
    Path(__file__).with_name('verify-eevee-completion.py'))
verifier = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(verifier)


def proof():
    rows = []
    for phase in (1, 2):
        state = {'render_frames': 10 * phase, 'document_revision': phase,
                 'undo_count': phase - 1, 'open_pending': False,
                 'composite_pending': False, 'property_pending': False, 'texture_loading': False}
        rows.append({'status': 'Captured', 'host_draw': str(10 * phase),
            'scene_target_generation': '1', 'width': 640, 'height': 480, 'active_view': 0,
            'sample_index': '1', 'sample_count': '64', 'sampling_finished': False,
            'viewport_sampling_finished': True, 'interactive': False, 'reset_pending': False,
            'needed_shaders_loaded': True, 'render_extent_valid': True,
            'queued_shaders': 0, 'queued_textures': 0, 'queued_optimizations': 17,
            'phase_at_Paint_entry': phase, 'current_phase': phase, 'paint_sequence': str(phase),
            'frames_before_Paint': 10 * phase - 1, 'frames_after_Paint': 10 * phase,
            'draw_before_Paint': str(10 * phase - 1), 'phase_draw_floor': str(10 * phase - 1),
            'revision_before_Paint': phase, 'revision_after_Paint': phase,
            'host_backend_route': {'active': False, 'device': 'EEVEE', 'status': 'EEVEE'},
            'fresh_current_scene_draw_and_later_Paint': True, 'terminal_sampling_state_observed': True,
            'target_ready': True, 'selected_material_uid': 7,
            'document_id': '{11111111-1111-1111-1111-111111111111}',
            'set_id': '{22222222-2222-2222-2222-222222222222}', 'state': state})
    return {'profile': 'eevee-completion-observation-only-v1', 'native_pid': 123,
            'passed': True, 'error': '', 'phase': 2, 'stage': 3,
            'natural_product_redraw_only': True, 'settings_written': False,
            'final_surface_acceptance': False, 'shader_math_changed': False,
            'snapshot_not_a_completion_epoch': True, 'observations': copy.deepcopy(rows),
            'terminal_sampling_observations': rows, 'state': copy.deepcopy(rows[1]['state']),
            'last_snapshot': copy.deepcopy(rows[1]), 'last_backend_route': copy.deepcopy(rows[1]['host_backend_route'])}


class FreshPostPaintObservation(unittest.TestCase):
    def test_two_current_phases_without_inventing_film_or_optimization_gate(self):
        self.assertTrue(verifier.valid_observation(proof(), 123))

    def test_cached_missing_or_replayed_phase_cannot_pass(self):
        mutations = [
            lambda p: p.update(terminal_sampling_observations=[]),
            lambda p: p.update(terminal_sampling_observations=[p['terminal_sampling_observations'][1]] * 2),
            lambda p: p.update(observations=[]),
            lambda p: p['observations'].reverse(),
            lambda p: p.update(native_pid=124),
            lambda p: p.update(passed=False),
            lambda p: p['state'].update(document_revision=3),
        ]
        for change in mutations:
            packet = proof(); change(packet)
            self.assertFalse(verifier.valid_observation(packet, 123))

    def test_native_value_and_Paint_identity_fail_closed(self):
        mutations = [
            ('status', 'Deferred'), ('host_draw', '19'), ('scene_target_generation', '0'),
            ('width', 0), ('active_view', 2), ('sample_count', '0'),
            ('interactive', True), ('reset_pending', True), ('needed_shaders_loaded', False),
            ('render_extent_valid', False), ('queued_shaders', 1), ('queued_textures', -1),
            ('frames_after_Paint', 19), ('revision_before_Paint', 1),
            ('fresh_current_scene_draw_and_later_Paint', False), ('target_ready', False),
            ('phase_at_Paint_entry', 1), ('current_phase', 1), ('paint_sequence', '1'),
            ('host_draw', True), ('queued_shaders', False), ('width', True),
        ]
        for key, value in mutations:
            packet = proof(); packet['terminal_sampling_observations'][1][key] = value
            self.assertFalse(verifier.valid_observation(packet, 123), (key, value))
        for change in [lambda r: r['state'].update(composite_pending=True),
                       lambda r: r['host_backend_route'].update(device='Cycles'),
                       lambda r: r['host_backend_route'].update(active=True),
                       lambda r: r.update(document_id='{33333333-3333-3333-3333-333333333333}')]:
            packet = proof(); change(packet['terminal_sampling_observations'][1])
            self.assertFalse(verifier.valid_observation(packet, 123))


if __name__ == '__main__':
    unittest.main()
