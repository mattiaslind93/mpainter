# SPDX-License-Identifier: GPL-3.0-or-later
"""Pure admission tests; never compile or launch Blender/Cycles."""
import copy
import importlib.util
import json
from pathlib import Path
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

SCRIPTS = Path(__file__).resolve().parents[2] / "scripts/host"
sys.path.insert(0, str(SCRIPTS))
spec = importlib.util.spec_from_file_location("displacement_candidate", SCRIPTS / "verify-cycles-displacement-candidate.py")
candidate = importlib.util.module_from_spec(spec)
spec.loader.exec_module(candidate)


def archive(path, members, symbol=b"index"):
    def entry(name, raw):
        header = f"{name:<16}{0:<12}{0:<6}{0:<6}{100644:<8}{len(raw):<10}`\n".encode()
        assert len(header) == 60
        return header + raw + (b"\n" if len(raw) & 1 else b"")
    data = b"!<arch>\n" + entry("/", symbol)
    for name, raw in members:
        data += entry(name + "/", raw)
    path.write_bytes(data)


class CandidateAdmission(unittest.TestCase):
    def test_exact_member_bytes_not_only_archive_or_member_receipt(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder); before, after, obj = (root / name for name in ("before.a", "after.a", "image.cpp.o"))
            archive(before, [("image.cpp.o", b"old"), ("same.o", b"unchanged")])
            obj.write_bytes(b"new")
            archive(after, [("image.cpp.o", b"new"), ("same.o", b"unchanged")], symbol=b"new index")
            stats, inventory = candidate.verify_archive(before, after, "image.cpp.o", obj)
            self.assertEqual(stats["unchanged_members"], 1)
            self.assertEqual(inventory["candidate_members_sha256"]["image.cpp.o"], candidate.sha256(obj))
            for members in ([('image.cpp.o', b'new'), ('same.o', b'wrong')],
                            [('image.cpp.o', b'new')], [('image.cpp.o', b'wrong'), ('same.o', b'unchanged')],
                            [('image.cpp.o', b'old'), ('same.o', b'unchanged')],
                            [('image.cpp.o', b'new'), ('image.cpp.o', b'new')]):
                archive(after, members)
                with self.assertRaises(ValueError):
                    candidate.verify_archive(before, after, "image.cpp.o", obj)

    def test_gnu_long_names_and_truncated_archive_are_not_silently_skipped(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "long.a"
            def entry(name, data):
                return f"{name:<16}{0:<12}{0:<6}{0:<6}{100644:<8}{len(data):<10}`\n".encode() + data + (b"\n" if len(data) & 1 else b"")
            path.write_bytes(b"!<arch>\n" + entry("//", b"a_very_long_name.cpp.o/\n") + entry("/0", b"bytes"))
            self.assertEqual(set(candidate.archive_members(path)), {"a_very_long_name.cpp.o"})
            path.write_bytes(path.read_bytes()[:-2])
            with self.assertRaises(ValueError): candidate.archive_members(path)

    def test_link_all_other_flags_objects_and_libraries_are_exact(self):
        base = ['c++', '-DNDEBUG', '-o', '/base/bin/host', '-Wl,--dependency-file=/base/integrated-link.d',
                '/base/lib/a.a', '/base/lib/b.a', '/base/tests/main.o', '/base/lib/a.a', '/base/lib/b.a', '/base/lib/b.a', '-lpthread']
        replacements = {'/base/lib/a.a': '/candidate/archives/a.a', '/base/lib/b.a': '/candidate/archives/b.a'}
        actual = ['c++', '-DNDEBUG', '-o', '/candidate/bin/host', '-Wl,--dependency-file=/candidate/candidate.link.d',
                  '/candidate/archives/a.a', '/candidate/archives/b.a', '/base/tests/main.o', '/candidate/archives/a.a', '/candidate/archives/b.a', '/candidate/archives/b.a', '-lpthread']
        check = lambda command: candidate.verify_relink(base, command, replacements, '/base/bin/host', '/candidate/bin/host')
        check(actual)
        for changed in (actual + ['/extra.o'], actual[:-1] + ['-lother'],
                        actual[:-2] + actual[-1:],
                        [x.replace('main.o', 'new-main.o') for x in actual],
                        [x.replace('-DNDEBUG', '-UNDEBUG') for x in actual]):
            with self.assertRaises(ValueError): check(changed)

    def test_same_three_test_objects_include_actual_byte_identity(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder); units = []
            for index, source in enumerate(sorted(candidate.TEST_UNITS)):
                obj = root / str(index); obj.write_bytes(source.encode())
                units.append({'source': source, 'source_sha256': 'source-' + str(index), 'object': str(obj),
                              'object_sha256': candidate.sha256(obj), 'command': ['exact flags', source]})
            candidate.verify_test_units(units, copy.deepcopy(units))
            changed = copy.deepcopy(units); changed[0]['command'] = ['different']
            with self.assertRaises(ValueError): candidate.verify_test_units(units, changed)
            with self.assertRaises(ValueError): candidate.verify_test_units(units, units[:2])
            Path(units[0]['object']).write_bytes(b'changed')
            with self.assertRaises(ValueError): candidate.verify_test_units(units, units)

    def test_link_only_finalization_preserves_one_compile_root_and_prior_failure(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder); artifacts = root / 'candidate2'; artifacts.mkdir()
            objects = [{'source': source, 'object': str((artifacts / 'objects' / source).with_suffix('.cpp.o'))}
                       for source in sorted(candidate.FORK_FILES - {'intern/cycles/scene/image.h'})]
            self.assertEqual(candidate.artifact_root({'compiled_units': objects}), artifacts)
            wrong = copy.deepcopy(objects); wrong[0]['object'] = wrong[0]['object'].replace('candidate2', 'another')
            with self.assertRaises(ValueError): candidate.artifact_root({'compiled_units': wrong})
            (artifacts / 'admission.json').write_text('successful source/compile admission')
            (artifacts / 'build-result.json').write_text(json.dumps({'actual_exit': 1, 'phase': 'link'}))
            admission_sha = candidate.sha256(artifacts / 'admission.json'); failure_sha = candidate.sha256(artifacts / 'build-result.json')
            archives = [{'original': '/base/a.a', 'member': 'image.cpp.o', 'member_inventory_sha256': 'inventory-a'},
                        {'original': '/base/b.a', 'member': 'sync.cpp.o', 'member_inventory_sha256': 'inventory-b'}]
            base = {'links': [{'command': ['/base/a.a', '/base/b.a', '/base/a.a', '/base/b.a', '/base/b.a']}]}
            final = {'kind': 'link-only', 'successful_compile_parent': str(artifacts), 'retained_prelink_failure_sha256': failure_sha,
                     'archive_token_multiplicity': {'/base/a.a': 2, '/base/b.a': 3}, 'new_compile_count': 0,
                     'archive_inventory_files': [{'path': str(artifacts / (item['member'] + '.archive-members.json')), 'sha256': item['member_inventory_sha256']} for item in archives]}
            record = {'archives': archives, 'finalization': final,
                      'parents': {str(artifacts / 'admission.json'): admission_sha, str(artifacts / 'build-result.json'): failure_sha}}
            with patch.multiple(candidate, COMPILE_ADMISSION=admission_sha, PRELINK_FAILURE=failure_sha):
                candidate.verify_finalization(record, artifacts, base)
                for key, value in [('new_compile_count', 1), ('kind', 'another-patch'),
                                   ('archive_token_multiplicity', {'/base/a.a': 3, '/base/b.a': 2}),
                                   ('retained_prelink_failure_sha256', 'wrong')]:
                    with self.assertRaises(ValueError, msg=key):
                        candidate.verify_finalization({**record, 'finalization': {**final, key: value}}, artifacts, base)

    def compile_fixture(self, root):
        private, snapshot = root / 'candidate', root / 'snapshot'
        private.mkdir(); snapshot.mkdir()
        relative_sources = ['intern/cycles/scene/image.cpp', 'intern/cycles/blender/sync.cpp']
        commands, units = [], []
        for relative in relative_sources:
            member = Path(relative).name + '.o'
            source = private / 'blender' / relative; source.parent.mkdir(parents=True, exist_ok=True); source.write_text(relative)
            original = ['c++', '-DNDEBUG', '-isystem', '/installed/blender/include', '-I/installed/build/generated',
                        '-MD', '-MT', 'old', '-MF', 'old.d', '-o', 'old.o', '-c', '/installed/blender/' + relative]
            commands.append(' '.join(original))
            obj = (private / 'objects' / relative).with_suffix('.cpp.o'); obj.parent.mkdir(parents=True, exist_ok=True); obj.write_bytes(b'object')
            actual = [x.replace('/installed/blender', str(private / 'blender')).replace('/installed/build', str(snapshot / 'integration-generated')) for x in original]
            for flag, value in [('-o', str(obj)), ('-MF', str(obj) + '.d'), ('-MT', str(obj))]: actual[actual.index(flag) + 1] = value
            Path(str(obj) + '.d').write_text('test: ' + str(source) + '\n')
            closure = {str(source): candidate.sha256(source)}
            closure_file = private / (member + '.consumed.json'); closure_file.write_text(json.dumps(closure))
            log = private / (member + '.log'); log.write_text('synthetic success receipt')
            units.append({'source': relative, 'original_command': original, 'command': actual, 'object': str(obj),
                          'object_sha256': candidate.sha256(obj), 'archive': 'lib/' + ('libcycles_scene.a' if 'scene/' in relative else 'libbf_intern_cycles.a'),
                          'member': member, 'closure_sha256': candidate.sha256(closure_file),
                          'compile': {'actual_exit': 0, 'command': actual, 'log': str(log), 'log_sha256': candidate.sha256(log)}})
        (snapshot / 'host-commands.json').write_text(json.dumps(commands))
        parent = {'blender_root': '/installed/blender', 'source_build': '/installed/build', 'blender_source_sha256':
                  {relative: candidate.sha256(private / 'blender' / relative) for relative in relative_sources}, 'generated_sha256': {}}
        return private, snapshot, parent, {'compiled_units': units, 'sources': []}

    def test_two_exact_compiles_and_actual_frozen_closure(self):
        with tempfile.TemporaryDirectory() as folder:
            private, snapshot, parent, record = self.compile_fixture(Path(folder))
            headers = {'headers': {}, 'external_sha256': {}}
            candidate.verify_compiles(record, private, snapshot, parent, headers)
            for field in ('flags', 'extra-unit', 'compile-exit'):
                bad = copy.deepcopy(record)
                if field == 'flags': bad['compiled_units'][0]['command'][1] = '-UNDEBUG'
                elif field == 'extra-unit': bad['compiled_units'].append(bad['compiled_units'][0])
                elif field == 'compile-exit': bad['compiled_units'][0]['compile']['actual_exit'] = 1
                with self.assertRaises(ValueError, msg=field): candidate.verify_compiles(bad, private, snapshot, parent, headers)
            obj = record['compiled_units'][0]['object']; extra = private / 'unknown.hh'; extra.write_text('not admitted')
            Path(obj + '.d').write_text('test: ' + str(extra) + '\n')
            with self.assertRaises(ValueError): candidate.verify_compiles(record, private, snapshot, parent, headers)

    def test_fork_extra_headers_are_only_the_pinned_original_two_tu_discovery(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder); private, snapshot, parent, record = self.compile_fixture(root)
            supplement_root = root / 'supplement'; supplement_root.mkdir()
            supplement = {'discovery': {}, 'discovery_commands': {}}
            for unit in record['compiled_units']:
                source = unit['source']; original = str(Path(parent['blender_root']) / source)
                dependency = supplement_root / (Path(source).stem + '.discovery.d')
                dependency.write_text('test: ' + original + '\n')
                expected = list(unit['original_command'])
                for flag in ('-o', '-MF', '-MT'):
                    index = expected.index(flag); del expected[index:index + 2]
                expected = [value for value in expected if value not in ('-c', '-MD', '-MMD', '-MP')]
                expected += ['-M', '-MF', str(dependency), '-MT', 'test-header-discovery']
                supplement['discovery'][source] = [{'path': original, 'sha256': parent['blender_source_sha256'][source]}]
                supplement['discovery_commands'][source] = {'command': expected, 'dependency_sha256': candidate.sha256(dependency)}
            path = supplement_root / 'test-headers.json'; path.write_text(json.dumps(supplement)); digest = candidate.sha256(path)
            item = {'path': str(path), 'sha256': digest, 'scope': 'fork-TU-extraheaders'}
            receipt = {'fork_tu_header_supplement': item, 'parents': {str(path): digest}}
            with patch.object(candidate, 'FORK_TU_HEADERS', digest), patch.object(candidate.integrated, 'validate_test_headers') as full_headers:
                candidate.verify_fork_supplement(receipt, snapshot, parent)
                full_headers.assert_called_once_with(snapshot, supplement_root, supplement)
                for changed in ({**item, 'scope': 'any-new-header'}, {**item, 'sha256': 'wrong'}, {**item, 'unknown': True}):
                    with self.assertRaises(ValueError): candidate.verify_fork_supplement({**receipt, 'fork_tu_header_supplement': changed}, snapshot, parent)
                with self.assertRaises(ValueError): candidate.verify_fork_supplement({**receipt, 'parents': {}}, snapshot, parent)
                changed = copy.deepcopy(supplement)
                changed['discovery_commands'][record['compiled_units'][0]['source']]['command'][1] = '-UNDEBUG'
                path.write_text(json.dumps(changed))
                with self.assertRaises(ValueError): candidate.verify_fork_supplement(receipt, snapshot, parent)

    def test_unknown_schema_or_wrong_fixed_parent_rejected_before_full_admission(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder); (root / 'build.json').write_text(json.dumps({'schema': 1, 'kind': 'some-other-patch'}))
            with self.assertRaisesRegex(ValueError, 'schema'):
                candidate.validate_candidate(root, root / 'baseline', root / 'repo')
        with patch.object(sys, 'argv', ['candidate', '--candidate', 'one', '--baseline', 'two', '--udim', '--run-only']):
            with self.assertRaises(SystemExit): candidate.main()

    def test_exact_baseline_build_run_parent_and_patch_are_all_required(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            (root / 'build.json').write_text('synthetic baseline build')
            (root / 'run-manifest.json').write_text('synthetic baseline run')
            build_sha = candidate.sha256(root / 'build.json'); run_sha = candidate.sha256(root / 'run-manifest.json')
            record = {'baseline_build_sha256': build_sha, 'parent_manifest_sha256': 'exact parent', 'patch_sha256': 'exact patch'}
            with patch.multiple(candidate, BASE_BUILD=build_sha, BASE_RUN=run_sha, BASE_PARENT='exact parent', PATCH='exact patch'):
                candidate.verify_fixed_parents(record, root)
                for name in record:
                    with self.assertRaises(ValueError, msg=name): candidate.verify_fixed_parents({**record, name: 'wrong'}, root)
                for name in ('build.json', 'run-manifest.json'):
                    path = root / name; before = path.read_bytes(); path.write_bytes(before + b' changed')
                    with self.assertRaises(ValueError, msg=name): candidate.verify_fixed_parents(record, root)
                    path.write_bytes(before)

    def test_negative_runner_exit_is_retained_when_post_identity_fails(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder); private = root / 'build/private'; private.mkdir(parents=True)
            (private / 'build.json').write_text('candidate build')
            output = private / 'runtime-driver'; output.mkdir(); (output / 'run-manifest.json').write_text('manifest')
            baseline = root / 'baseline'; baseline.mkdir()
            arguments = ['candidate', '--candidate', str(private), '--baseline', str(baseline), '--run-only']
            with patch.object(candidate, '__file__', str(root / 'repo/scripts/host/candidate.py')), \
                    patch.object(sys, 'argv', arguments), \
                    patch.object(candidate, 'validate_candidate', side_effect=[({}, {}, root / 'snapshot', []), ValueError('changed after run')]), \
                    patch.object(candidate, 'prepare_runtime', return_value=output), \
                    patch.object(candidate.subprocess, 'run', return_value=SimpleNamespace(returncode=1)):
                with self.assertRaisesRegex(ValueError, 'changed after run'): candidate.main()
            observed = json.loads((private / 'runtime-post-admission.json').read_text())
            self.assertEqual(observed['actual_runner_exit'], 1)
            self.assertIs(observed['post_identity_passed'], False)


if __name__ == '__main__':
    unittest.main()
