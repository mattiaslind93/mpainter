# SPDX-License-Identifier: GPL-3.0-or-later
"""Pure artifact/progress-boundary tests; synthetic oldProbe is not a9ad2 claim."""
import copy
import importlib.util
import json
from pathlib import Path
from types import SimpleNamespace
import sys
import tempfile
import unittest
from unittest.mock import patch

HOST = Path(__file__).resolve().parents[2] / 'scripts/host'
sys.path.insert(0, str(HOST))
spec = importlib.util.spec_from_file_location('observer_runtime', HOST / 'run-cycles-default-observer.py')
m = importlib.util.module_from_spec(spec); spec.loader.exec_module(m)
WRITER = m.writer_module(HOST / 'verify-cycles-default-observer.py')

class RuntimeAdmission(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(); self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def file(self, name, data=b'original'):
        path = self.root / name; path.parent.mkdir(parents=True, exist_ok=True); path.write_bytes(data)
        return path

    def objects(self):
        parent_archive = self.file('libproduction.a')
        new = {name: WRITER.record(self.file(name + '.o')) for name in ('actor', 'probe')}
        old = self.file('retained-original-probe.o')
        retained = {str(old): {'sha256': m.sha(old), 'linked': False}}
        parent = {'link_inputs': {'libproduction.a': {'kind': 'frozen', 'path': 'libproduction.a', 'sha256': m.sha(parent_archive)}}}
        command = ['c++', str(old), new['actor']['path'], new['probe']['path'], str(parent_archive), str(parent_archive)]
        return parent, new, old, retained, command

    def test_complete_actual_input_map_and_repeated_archives(self):
        parent, new, old, retained, command = self.objects()
        result = m.link_controls(command, parent, self.root, retained, new)
        self.assertTrue(retained[str(old)]['linked'])
        self.assertEqual(result[str(self.root / 'libproduction.a')]['token_indices'], [4, 5])
        self.assertEqual(len(result), 4)
        parent['link_inputs']['unused.a'] = {'kind': 'frozen', 'path': 'unused.a', 'sha256': 'not-consumed'}
        self.assertEqual(m.link_controls(command, parent, self.root, retained, new), result)

    def test_changed_retained_original_probe_and_new_object_reject(self):
        parent, new, old, retained, command = self.objects()
        old.write_bytes(b'changed')
        with self.assertRaisesRegex(ValueError, 'Changed actual link input'):
            m.link_controls(command, parent, self.root, retained, new)
        old.write_bytes(b'original'); Path(new['probe']['path']).write_bytes(b'changed')
        with self.assertRaisesRegex(ValueError, 'Changed actual link input'):
            m.link_controls(command, parent, self.root, retained, new)

    def test_extra_archive_missing_new_object_or_changed_archive_reject(self):
        parent, new, old, retained, command = self.objects()
        unknown = self.file('extra.a')
        with self.assertRaisesRegex(ValueError, 'Unknown actual link input'):
            m.link_controls(command + [str(unknown)], parent, self.root, retained, new)
        with self.assertRaisesRegex(ValueError, 'Missing new observer object'):
            m.link_controls([x for x in command if x != new['actor']['path']], parent, self.root, retained, new)
        (self.root / 'libproduction.a').write_bytes(b'changed')
        with self.assertRaisesRegex(ValueError, 'Changed actual link input'):
            m.link_controls(command, parent, self.root, retained, new)

    def test_all_retained_donor_tests_sources_and_compiled_deps(self):
        common = self.file('header.hh')
        units, scans, allowed = [], {}, {str(common): m.sha(common)}
        for name in ('main', 'cache-cpu-unused', 'retained-oldProbe'):
            source = self.file(name + '.cc'); obj = self.file(name + '.o')
            scan = self.file(name + '.scan.d', ('target: ' + str(source) + ' ' + str(common) + '\n').encode())
            md = self.file(name + '.o.d', scan.read_bytes())
            allowed[str(source)] = m.sha(source)
            units.append({'source': 'native/tests/' + name + '.cc', 'source_sha256': m.sha(source),
                          'object': str(obj), 'object_sha256': m.sha(obj), 'command': ['c++', '-c', str(source), '-MF', str(md)]})
            scans[name + '.cc'] = {'command': ['c++', '-MF', str(scan)], 'dependency_sha256': m.sha(scan)}
        units.append({'source': 'native/tests/cycles_live_udim_artist_test.cc', 'object': 'replaced-oldActor.o'})
        donor = {'compiled_test_units': units, 'frozen_preprocessor_closure': scans}
        options = SimpleNamespace(snapshot=self.root, baseline=self.root)
        with patch.object(WRITER, 'headers', return_value=allowed):
            rows = m.reused_tests(WRITER, options, {}, donor)
            self.assertEqual(len(rows), 3)
            self.assertTrue(all(not row['linked'] for row in rows.values()))
            self.assertIn(str(self.root / 'cache-cpu-unused.o'), rows)
            (self.root / 'retained-oldProbe.o').write_bytes(b'changed')
            with self.assertRaisesRegex(ValueError, 'Changed retained donor test object'):
                m.reused_tests(WRITER, options, {}, donor)
            (self.root / 'retained-oldProbe.o').write_bytes(b'original')
            common.write_bytes(b'changed')
            with self.assertRaisesRegex(ValueError, 'Unadmitted consumed dependency'):
                m.reused_tests(WRITER, options, {}, donor)

    def trace(self):
        events = []
        for generation, epochs in ((1, 5), (2, 1)):
            sequence = 0
            for epoch in range(1, epochs + 1):
                observations = [(0, 1, 'Rendering Done'), (1, .1, 'Rendering'), (64, 1, 'Rendering Done')]
                for sample, progress, status in observations:
                    sequence += 1
                    events.append({'generation': generation, 'epoch': epoch, 'sequence': sequence, 'sample': sample,
                                   'progress': progress, 'status': status, 'steady_ns': len(events) * 10 + 10})
        sessions = [{'generation': gen, 'viewport': True, 'background': False, 'auto_tile': False} for gen in (1, 2)]
        return {'sessions': sessions, 'events': events, 'failed': False, 'failure': ''}

    def test_actual_active_adaptive_terminals_without_literal128(self):
        terminals = m.terminal_events(self.trace())
        self.assertEqual(len(terminals), 6)
        self.assertTrue(all(row['sample'] == 64 for row in terminals.values()))
        trace = self.trace(); trace['events'] = [row for row in trace['events'] if row['status'] == 'Rendering Done']
        self.assertEqual(m.terminal_events(trace), {})

    def test_invalid_progress_epoch_session_or_callback_failure_reject(self):
        for edit in (lambda x: x['events'][1].update(progress=float('nan')),
                     lambda x: x['events'][1].update(sample=True),
                     lambda x: x['events'][3].update(epoch=1),
                     lambda x: x['sessions'][0].update(auto_tile=True),
                     lambda x: x.update(failed=True)):
            trace = self.trace(); edit(trace)
            with self.assertRaises(ValueError): m.terminal_events(trace)

    def complete_fixture(self):
        trace = self.trace(); terminals = list(m.terminal_events(trace).values())
        settings = {'adaptive': True, 'pixel_jitter': True, 'denoise': True, 'samples': 128, 'frame': 1,
                    'color_management': {'working_space': 'ACEScg'}}
        rows = []
        for stage, event in zip(m.STAGES, terminals):
            completion = {key: event[key] for key in ('generation', 'epoch', 'sequence', 'sample', 'progress', 'status')}
            completion.update(terminal_epoch=event['epoch'], draw_generation=event['generation'], draw_epoch=event['epoch'],
                              paint_begin_ns=event['steady_ns'] + 1, paint_done_ns=event['steady_ns'] + 2)
            rows.append({'stage': stage, 'actual_completion': completion, 'observed_default_settings': copy.deepcopy(settings)})
        (self.root / 'cycles-udim.json').write_text(json.dumps({'profile': m.PROFILE, 'captures': rows}))
        (self.root / 'default-completion-trace.json').write_text(json.dumps(trace))
        for a, b in [('cycles-before', 'cycles-color-undone'), ('cycles-color-edited', 'cycles-color-redone')]:
            self.file(a + '-mesh-roi.png', a.encode()); self.file(b + '-mesh-roi.png', a.encode())
        log = 'BTS_SCENE_READY pid=77 native_window=none\nBTS_GPU_READY pid=77 renderer=NVIDIA RTX\nBTS_CYCLES_DEVICE GPU [\'NVIDIA RTX\']\n'
        for phase in ('IDLE', 'CAMERA'): log += 'BTS_BA_' + phase + '_BEGIN\nBTS_BA_' + phase + '_END\n'
        log += 'BTS_BA_DEFAULT_OBSERVED_V2_DONE\nBTS_SHUTDOWN clean pid=77\n'
        (self.root / 'run.log').write_text(log)
        return rows

    def test_private_environment_strips_inherited_profiles_and_desktop_paths(self):
        inherited = {'BTS_BA_CONTROLLED_PROFILE': '1', 'BTS_CYCLES_DEVICE': 'CPU', 'BTS_ROOT': '/artist',
                     'BTS_UNRELATED_DEMO': '1', 'XDG_CONFIG_HOME': '/artist/config', 'XDG_RUNTIME_DIR': '/run/user/1',
                     'QT_QPA_PLATFORM': 'xcb', 'QT_SCALE_FACTOR': '2', 'WAYLAND_DISPLAY': 'wayland-0',
                     'DISPLAY': ':1', 'XAUTHORITY': '/private/auth', 'PATH': '/usr/bin', 'LANG': 'en_US.UTF-8'}
        env = m.private_environment(inherited, self.root, Path('/bts'), Path('/frozen'), Path('/binary'), Path('/env.hdr'))
        for key in ('BTS_BA_CONTROLLED_PROFILE', 'BTS_CYCLES_DEVICE', 'BTS_UNRELATED_DEMO', 'QT_SCALE_FACTOR', 'WAYLAND_DISPLAY'):
            self.assertNotIn(key, env)
        self.assertEqual(env['XDG_CONFIG_HOME'], str(self.root / 'config'))
        self.assertEqual(env['XDG_RUNTIME_DIR'], str(self.root / 'runtime'))
        self.assertEqual(env['QT_QPA_PLATFORM'], 'offscreen')
        self.assertEqual(env['BTS_ROOT'], '/bts')
        self.assertEqual((env['LANG'], env['LC_ALL']), ('sv_SE.UTF-8', 'sv_SE.UTF-8'))
        self.assertEqual((env['DISPLAY'], env['XAUTHORITY']), (':1', '/private/auth'))
        self.assertEqual(env['PATH'], '/usr/bin')

    def test_clean_pid_prefix_and_duplicate_markers_reject(self):
        self.complete_fixture()
        path = self.root / 'run.log'; original = path.read_text()
        for changed in (original.replace('clean pid=77', 'clean pid=770'),
                        original.replace('clean pid=77', 'clean pid=77suffix'),
                        original + 'BTS_SHUTDOWN clean pid=77\n'):
            path.write_text(changed)
            with self.assertRaisesRegex(ValueError, 'bounded clean marker'):
                m.complete_attempt(self.root, 77, 0)
        path.write_text(original)
        self.assertEqual(m.complete_attempt(self.root, 77, 0)['native_pid'], 77)

    def test_completion_requires_real_exit_pid_draw_epoch_and_strict_roi(self):
        rows = self.complete_fixture()
        self.assertEqual(m.complete_attempt(self.root, 77, 0)['native_returncode'], 0)
        with self.assertRaises(ValueError): m.complete_attempt(self.root, 77, -11)
        with self.assertRaises(ValueError): m.complete_attempt(self.root, 78, 0)
        rows[2]['actual_completion']['draw_epoch'] = 1
        (self.root / 'cycles-udim.json').write_text(json.dumps({'profile': m.PROFILE, 'captures': rows}))
        with self.assertRaises(ValueError): m.complete_attempt(self.root, 77, 0)
        self.complete_fixture(); (self.root / 'cycles-color-undone-mesh-roi.png').write_bytes(b'one-LSB-negative-retained')
        with self.assertRaises(ValueError): m.complete_attempt(self.root, 77, 0)

if __name__ == '__main__':
    unittest.main()
