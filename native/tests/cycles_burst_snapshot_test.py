"""Synthetic source/parent/build admission; mocked compiler, no preprocessing/GPU."""
import json
from pathlib import Path
import shutil
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'scripts/host'))
import cycles_burst_snapshot as burst
import cycles_integrated_snapshot as integrated
import cycles_integrated_snapshot_test as base_tests
from cycles_live_manifest import sha256, validate_manifest, required_sources

REAL = Path(__file__).resolve().parents[2]


class BurstSnapshotTests(unittest.TestCase):
    def fixture(self, root):
        repo, snapshot, *_ = base_tests.IntegratedSnapshotTests().fixture(root, cycles=True)
        # Expand the deliberately tiny synthetic integration to the runtime
        # manifest's required core set; these bytes are never compiled.
        manifest = snapshot / 'integrated-snapshot.json'
        meta = json.loads(manifest.read_text())
        for relative in required_sources('udim') - set(meta['native_source_sha256']):
            for folder in (repo, snapshot / 'integration-source'):
                target = folder / relative; target.parent.mkdir(parents=True, exist_ok=True)
                target.write_text('synthetic uncompiled runtime dependency ' + relative)
            meta['native_source_sha256'][relative] = sha256(repo / relative)
        manifest.write_text(json.dumps(meta))
        def run(args, **kwargs):
            command = args[3:]
            if '-M' in command:
                source = next(value for value in command if value.endswith('.cc'))
                Path(command[command.index('-MF') + 1]).write_text('test: ' + source + '\n')
            else:
                Path(command[command.index('-o') + 1]).write_bytes(b'synthetic test-only artifact')
            return SimpleNamespace(returncode=0)
        default = root / 'default'; default.mkdir()
        with patch.object(integrated.subprocess, 'run', side_effect=run):
            integrated.build_drivers(snapshot, repo, default, True, {})
        for relative in burst.BE_SOURCES:
            target = repo / relative; target.parent.mkdir(parents=True, exist_ok=True)
            raw = (REAL / relative).read_bytes()
            if relative == burst.FIXTURE:
                raw = raw.replace(burst.FRIEND, b'', 1)
                # This parent represents the historical reviewed collector,
                # before the current removal of memory preflight refusal.
                raw = raw.replace(b'  if(!diagnostic_readback){\n\n   diagnostic_stage=',
                    b'  if(!diagnostic_readback){\n   require_texture_ram(std::uint64_t(resolution)*resolution*16*4+16ULL*1024*1024,"Controlled diagnostic full tile hashes");\n   diagnostic_stage=', 1)
            target.write_bytes(raw)
        controlled = root / 'controlled'; controlled.mkdir()
        with patch.object(integrated.subprocess, 'run', side_effect=run):
            integrated.build_drivers(snapshot, repo, controlled, True, {}, True, default / 'run-manifest.json')
        for relative in burst.BURST_SOURCES:
            shutil.copyfile(REAL / relative, repo / relative)
        target = repo / burst.FIXTURE
        target.write_bytes((REAL / burst.FIXTURE).read_bytes())
        shutil.copyfile(REAL / 'native/tests/cycles_live_burst_plan.md', repo / 'native/tests/cycles_live_burst_plan.md')
        for name in ('cycles_burst_snapshot.py', 'check-cycles-live-burst.py'):
            shutil.copyfile(REAL / 'scripts/host' / name, repo / 'scripts/host' / name)
        return repo, snapshot, default / 'run-manifest.json', controlled / 'run-manifest.json', run

    def previous(self, root):
        previous = root / 'prior-three-second-attempt'; previous.mkdir()
        for relative in burst.PREVIOUS_EVIDENCE:
            target = previous / relative; target.parent.mkdir(parents=True, exist_ok=True)
            target.write_text('synthetic retained inconclusive three-second evidence ' + relative)
        return previous, {relative: sha256(previous / relative) for relative in burst.PREVIOUS_EVIDENCE}

    def test_original_inconclusive_evidence_is_independent_immutable_parent(self):
        with tempfile.TemporaryDirectory() as directory:
            previous, expected = self.previous(Path(directory))
            with patch.dict(burst.PREVIOUS_EVIDENCE, expected, clear=True):
                records = burst.validate_previous_attempt(previous)
                self.assertEqual(len(records), 4)
                path = previous / 'inconclusive-summary.json'; raw = path.read_bytes()
                path.write_bytes(raw + b'changed')
                with self.assertRaises(ValueError): burst.validate_previous_attempt(previous)
                path.write_bytes(raw)
                with self.assertRaises(ValueError): burst.validate_previous_attempt(None)
                burst.validate_previous_attempt(previous)

    def test_fixed_sources_friend_and_constructor_reject_mutation(self):
        with tempfile.TemporaryDirectory() as directory:
            repo, snapshot, *_ = self.fixture(Path(directory))
            overlay = burst.overlay(snapshot, repo)
            self.assertEqual(set(overlay), burst.TEST_SOURCES)
            self.assertTrue(overlay[burst.FIXTURE]['memory_preflight_removed'])
            self.assertFalse(overlay[burst.FIXTURE]['friend_only'])
            admitted = integrated.validate(snapshot, repo, overlay, burst.PROFILE)
            self.assertIsNone(overlay['native/tests/CyclesLiveBurst.hh']['original_sha256'])
            self.assertEqual(admitted['kind'], 'full-native-fork-integration')
            for bad in ({}, {key: value for key, value in overlay.items() if key != burst.FIXTURE},
                        {**overlay, 'native/host/main.cc': {}}):
                with self.assertRaises(ValueError):
                    burst.validate_overlay(snapshot, repo, bad)
            with self.assertRaises(ValueError):
                integrated.validate(snapshot, repo, overlay, integrated.CONTROLLED_PROFILE)
            path = repo / burst.FIXTURE; raw = path.read_bytes()
            for broken in (raw.replace(burst.FRIEND, b''), raw.replace(burst.FRIEND, burst.FRIEND * 2), raw + b'// extra fixture change'):
                path.write_bytes(broken)
                with self.assertRaises(ValueError):
                    burst.validate_sources(repo)
            path.write_bytes(raw)
            path = repo / 'native/tests/cycles_live_burst_test.cc'; original = path.read_text()
            for broken in (original.replace('std::move(edit),CyclesFrameHook{})', 'std::move(edit))'),
                           original + '\n#define BTS_BA_CONTROLLED_PROFILE_TEST 1\n'):
                path.write_text(broken)
                with self.assertRaises(ValueError):
                    burst.validate_sources(repo)
            path.write_text(original)
            burst.validate_overlay(snapshot, repo, overlay)

    def test_two_test_units_parent_cache_and_no_production_changes(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory); repo, snapshot, default, controlled, run = self.fixture(root)
            output = root / 'burst'; output.mkdir()
            previous, prior_hashes = self.previous(root)
            inputs = {path: path.read_bytes() for path in (snapshot / 'objects').glob('*')}
            with patch.object(integrated.subprocess, 'run', side_effect=run), patch.dict(burst.PREVIOUS_EVIDENCE, prior_hashes, clear=True):
                record = integrated.build_drivers(snapshot, repo, output, True, {}, False, default, True, controlled, previous)
            self.assertEqual([Path(item['source']).name for item in record['compiled_test_units']], list(burst.COMPILED_TEST_NAMES))
            self.assertEqual(record['production_units_recompiled'], [])
            self.assertIs(record['fork_patches_reapplied'], False)
            for item in record['compiled_test_units']:
                self.assertNotIn('-DBTS_BA_CONTROLLED_PROFILE_TEST=1', item['command'])
                self.assertIn('-DNDEBUG', item['command'])
            link = record['links'][0]['command']
            self.assertEqual(sum(Path(v).name == 'cycles_live_burst_test.o' for v in link), 1)
            self.assertFalse(any(Path(v).name in ('CyclesLiveCompletionProbe.o', 'cycles_live_udim_artist_test.o') for v in link))
            self.assertEqual((output / 'bin/cycles-cache-cpu').read_bytes(), (default.parent / 'bin/cycles-cache-cpu').read_bytes())
            for path, content in inputs.items(): self.assertEqual(path.read_bytes(), content)
            manifest = output / 'run-manifest.json'; binary = output / 'bin/cycles-live-artist'
            validate_manifest(manifest, repo, binary, 'udim', False, burst.PROFILE)
            with self.assertRaises(ValueError): validate_manifest(manifest, repo, binary, 'udim', False, 'default')
            admitted = integrated.validate(snapshot, repo, record['test_overlay'], burst.PROFILE)
            integrated.validate_overlay_copy(output, admitted, record['test_overlay'], True)
            path = output / 'burst-source/native/host/Example.hh'; path.write_text('stale copy')
            with self.assertRaises(ValueError): integrated.validate_overlay_copy(output, admitted, record['test_overlay'], True)

    def test_parents_and_imported_policy_cannot_change_unnoticed(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory); repo, snapshot, default, controlled, run = self.fixture(root)
            overlay = burst.overlay(snapshot, repo); admitted = integrated.validate(snapshot, repo, overlay, burst.PROFILE)
            parent = integrated.controlled_parent(default, admitted, snapshot)[0]
            burst.validate_controlled_parent(controlled, admitted, snapshot, parent)
            raw = controlled.read_text()
            for mutation in ('profile', 'source', 'parent', 'binary'):
                obj = json.loads(raw)
                if mutation == 'profile': obj['profile'] = 'default'
                elif mutation == 'source': obj['test_overlay'][burst.FIXTURE]['sha256'] = '0' * 64
                elif mutation == 'parent': obj['parents'] = []
                else: obj['binaries'][next(iter(obj['binaries']))] = '0' * 64
                controlled.write_text(json.dumps(obj))
                with self.assertRaises(ValueError): burst.validate_controlled_parent(controlled, admitted, snapshot, parent)
            controlled.write_text(raw)
            with self.assertRaises(ValueError): burst.validate_controlled_parent(None, admitted, snapshot, parent)
            # /home and /var/home aliases identify the same retained artifact.
            alias = root / 'alias'; alias.symlink_to(root, target_is_directory=True)
            aliased = json.loads(raw)
            build_path = controlled.parent / 'build.json'; original_build = build_path.read_text()
            build = json.loads(original_build)
            build['default_parent']['path'] = str(alias / Path(parent['path']).relative_to(root))
            build_path.write_text(json.dumps(build))
            for item in aliased['parents']:
                original_path = Path(item['path'])
                if original_path == build_path: item['sha256'] = sha256(build_path)
                item['path'] = str(alias / original_path.relative_to(root))
            controlled.write_text(json.dumps(aliased))
            burst.validate_controlled_parent(controlled, admitted, alias / snapshot.relative_to(root), parent)
            build_path.write_text(original_build); controlled.write_text(raw)
            previous, prior_hashes = self.previous(root)
            for name in ('cycles_burst_snapshot.py', 'check-cycles-live-burst.py'):
                path = repo / 'scripts/host' / name; original = path.read_bytes()
                identities = integrated.runner_identities(repo, True)
                path.write_bytes(original + b'\n# changed policy\n')
                with self.assertRaises(ValueError): integrated.validate_runner_sources(repo, identities, True)
                path.write_bytes(original)
                output = root / name; output.mkdir()
                def changed(args, **kwargs):
                    result = run(args, **kwargs)
                    if '-M' not in args and '-c' not in args: path.write_bytes(original + b'\n# mid-build change\n')
                    return result
                with patch.object(integrated.subprocess, 'run', side_effect=changed), patch.dict(burst.PREVIOUS_EVIDENCE, prior_hashes, clear=True), self.assertRaisesRegex(ValueError, 'Runner source changed'):
                    integrated.build_drivers(snapshot, repo, output, True, {}, False, default, True, controlled, previous)
                path.write_bytes(original)


if __name__ == '__main__': unittest.main()
