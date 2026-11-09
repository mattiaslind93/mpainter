# SPDX-License-Identifier: GPL-3.0-or-later
"""Small metadata-admission negatives; no compiler, Blender, or GPU execution."""
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
spec = importlib.util.spec_from_file_location('default_observer', HOST / 'verify-cycles-default-observer.py')
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)

class Admission(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.out = self.root / 'out'
        self.out.mkdir()
        self.snapshot = self.root / 'snapshot'
        self.baseline = self.root / 'baseline'
        self.probe = self.root / 'probe'
        for folder in (self.snapshot, self.baseline, self.probe):
            folder.mkdir()
        self.options = SimpleNamespace(snapshot=self.snapshot, baseline=self.baseline, probe_donor=self.probe,
                                       syntax_receipt=self.root / 'syntax.json')
        for path in (self.snapshot / 'integrated-snapshot.json', self.baseline / 'build.json',
                     self.baseline / 'run-manifest.json', self.probe / 'build.json', self.options.syntax_receipt):
            path.write_text('{}')
        (self.out / 'objects').mkdir()
        for name in ('actor', 'probe'):
            (self.out / 'objects' / (name + '.o')).write_bytes(name.encode())
        (self.out / 'bin').mkdir()
        (self.out / 'bin/cycles-live-artist').write_bytes(b'not-an-executable; metadata-only fixture')
        self.main = self.root / 'old-main.o'
        self.main.write_bytes(b'original-main')
        self.old_actor = str(self.root / 'old-actor.o')
        def unit(old, extra=()):
            return {'source': 'native/tests/' + old, 'object': self.old_actor,
                    'command': ['c++', *extra, '-DNDEBUG', '-I/base/native/host', '-MD',
                                '-MT', 'old.o', '-MF', 'old.o.d', '-o', 'old.o', '-c', '/base/native/tests/' + old]}
        actor = unit(m.PAIRS['actor'][0])
        observer = unit(m.PAIRS['probe'][0], ['-DCCL_NAMESPACE_BEGIN=namespace ccl {'])
        main = {'source': 'native/tests/cycles_live_udim_artist_main.cc', 'object': str(self.main),
                'object_sha256': m.sha(self.main)}
        link = ['c++', str(self.main), self.old_actor, '-Wl,--dependency-file=old.d', '-o', 'old-bin',
                'libscene.a', 'libscene.a', 'libcycles.a', 'libcycles.a', 'libcycles.a', 'real-GuiFixDemo.o']
        original = {'binary': '/old/bin/cycles-live-artist', 'command': link, 'sha256': 'unused-default-binary'}
        self.default = {'compiled_test_units': [main, actor], 'links': [original]}
        self.observer = {'compiled_test_units': [observer]}
        self.parent = {'native_source_sha256': {}}
        self.receipt = {'profile': m.PROFILE, 'sources': m.SOURCES, 'production_units_recompiled': [],
                        'fork_patches_reapplied': False, 'writer': m.record(Path(m.__file__)),
                        'parents': [m.record(self.snapshot / 'integrated-snapshot.json'),
                                    m.record(self.baseline / 'build.json'), m.record(self.baseline / 'run-manifest.json'),
                                    m.record(self.probe / 'build.json'), m.record(self.options.syntax_receipt)],
                        'compiled_test_units': {}, 'scans': {}, 'retained_main': main, 'original_link': original}
        for label, donor in [('actor', actor), ('probe', observer)]:
            obj = self.out / 'objects' / (label + '.o')
            command = m.compile_command(donor, m.PAIRS[label][1], self.out / 'test-source', obj)
            self.receipt['compiled_test_units'][label] = {'object': m.record(obj), 'command': command,
                                                         'returncode': 0, 'dependencies': {}}
            self.receipt['scans'][label] = {'returncode': 0, 'dependencies': {},
                'command': m.integration.dependency_scan(command, self.out / (label + '.preprocess.d'))}
        binary = m.record(self.out / 'bin/cycles-live-artist')
        self.receipt['link'] = {'returncode': 0, 'binary': binary,
            'command': m.link_command(link, self.old_actor, str(self.out / 'objects/actor.o'),
                                      str(self.out / 'objects/probe.o'), binary['path'], str(self.out / 'integrated-link.d'))}
        self.manifest = {'profile': m.PROFILE, 'binaries': {binary['path']: binary['sha256']}}
        self.flush()
        self.real_closure = m.closure
        self.contexts = [patch.object(m, 'parents', return_value=(self.parent, self.default, self.observer, {})),
                         patch.object(m, 'headers', return_value={}), patch.object(m, 'closure', return_value={})]
        for context in self.contexts:
            context.start(); self.addCleanup(context.stop)

    def flush(self):
        (self.out / 'build.json').write_text(json.dumps(self.receipt))
        self.manifest['parents'] = [m.record(self.out / 'build.json')]
        (self.out / 'run-manifest.json').write_text(json.dumps(self.manifest))

    def admitted(self):
        return m.admit(self.options, self.root, self.out)

    def rejects(self, change):
        before, manifest = copy.deepcopy(self.receipt), copy.deepcopy(self.manifest)
        change(); self.flush()
        with self.assertRaises(ValueError): self.admitted()
        self.receipt, self.manifest = before, manifest
        self.flush()

    def test_exact_two_objects_and_repeated_archives_positive(self):
        self.assertEqual(self.admitted()['profile'], m.PROFILE)
        args = self.receipt['link']['command']
        self.assertEqual(args.count('libscene.a'), 2)
        self.assertEqual(args.count('libcycles.a'), 3)
        self.assertEqual(args.count('real-GuiFixDemo.o'), 1)
        self.assertEqual(len(args), len(self.receipt['original_link']['command']) + 1)

    def test_other_profiles_and_parent_change_rejected(self):
        self.rejects(lambda: self.receipt.update(profile='default'))
        self.rejects(lambda: self.manifest.update(profile='controlled-fixed-sampling-v1'))
        self.rejects(lambda: self.receipt['parents'].pop())
        self.rejects(lambda: self.receipt['sources'].update({'native/host/BlenderHost.cc': 'foreign'}))

    def test_exact_flags_and_extra_object_rejected(self):
        self.rejects(lambda: self.receipt['compiled_test_units']['probe']['command'].append('-DBTS_BA_CONTROLLED_PROFILE_TEST=1'))
        self.rejects(lambda: self.receipt['compiled_test_units']['actor']['command'].remove('-DNDEBUG'))
        self.rejects(lambda: self.receipt['compiled_test_units'].update(extra=copy.deepcopy(self.receipt['compiled_test_units']['actor'])))
        self.rejects(lambda: self.receipt.update(production_units_recompiled=['BlenderHost.cc']))
        self.rejects(lambda: self.receipt.update(fork_patches_reapplied=True))

    def test_arbitrary_link_change_and_stale_objects_rejected(self):
        self.rejects(lambda: self.receipt['link']['command'].append('foreign.o'))
        self.rejects(lambda: self.receipt['link']['command'].remove('libscene.a'))
        self.rejects(lambda: self.receipt['retained_main'].update(object_sha256='wrong'))
        (self.out / 'objects/probe.o').write_bytes(b'changed')
        with self.assertRaises(ValueError): self.admitted()

    def test_stale_writer_and_binary_rejected(self):
        self.rejects(lambda: self.receipt['writer'].update(sha256='wrong'))
        (self.out / 'bin/cycles-live-artist').write_bytes(b'changed')
        with self.assertRaises(ValueError): self.admitted()

    def test_real_dependency_guard_unknown_or_changed(self):
        # This subtest bypasses the fixture's closure mock and uses actual bytes.
        real_closure = self.real_closure
        source = self.root / 'header.hh'; source.write_text('original')
        dep = self.root / 'scan.d'; dep.write_text('target: ' + str(source) + '\n')
        allowed = {str(source.resolve()): m.sha(source)}
        self.assertEqual(real_closure(dep, self.snapshot, allowed), {str(source): m.sha(source)})
        with self.assertRaises(ValueError): real_closure(dep, self.snapshot, {})
        source.write_text('changed')
        with self.assertRaises(ValueError): real_closure(dep, self.snapshot, allowed)

    def test_fixed_artifact_hash_rejects_changed_parent(self):
        path = self.root / 'fixed.json'; path.write_text('{}')
        digest = m.sha(path)
        self.assertEqual(m.fixed_file(path, digest), {})
        path.write_text('{"changed":true}')
        with self.assertRaises(ValueError): m.fixed_file(path, digest)

if __name__ == '__main__':
    unittest.main()
