"""Pure admission negatives; never starts a compiler, Qt or native executable."""
import copy
import hashlib
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import cycles_captured_verifier as v

def record():
    return {'kind': 'integrated-captured-revision-followup', 'profile': v.PROFILE,
            'production_units_recompiled': [], 'fork_patches_reapplied': False,
            'actor_identity': v.ACTOR_SHA.copy(), 'positive_minimum_publications': 2,
            'compiled_test_units': [{'source': name, 'source_sha256': digest, 'command': ['c++', '-DNDEBUG']}
                                    for name, digest in list(v.ACTOR_SHA.items())[:2]],
            'links': [{'binary': 'new-integrated-artist'}],
            'historical_evidence': [{'path': 'old-negative', 'role': 'historical-only'}]}

class CapturedVerifierAdmission(unittest.TestCase):
    def test_whole_log_requires_clean_exit_without_native_diagnostics(self):
        clean = '[06:00:00] BTS_SHUTDOWN clean pid=123\n'
        v.require_clean_exit(0, 'ordinary cold-start warning\n' + clean)
        for code, log in ((1,clean),(0,'actor complete\n'),(0,'BTS_SHUTDOWN cleanliness\n')):
            with self.assertRaises(RuntimeError): v.require_clean_exit(code,log)
        for diagnostic in ('BTS_GPU_STACK_ERROR failed','BTS_GPU_STACK_FALLBACK path',
                           'BTS_NATIVE_CLEANUP_FAILED','BTS_HOST_FAILED','BTS_DRAW_FAILED',
                           'AddressSanitizer: error','Not freed memory','Shader compilation failed',
                           'OpenColorIO Error','Could not find color space','color_management | ERROR'):
            for log in (diagnostic+'\nHELD_BEGIN\nHELD_END\n'+clean,
                        'HELD_BEGIN\nHELD_END\n'+clean+diagnostic):
                with self.subTest(diagnostic=diagnostic,log=log):
                    with self.assertRaises(RuntimeError): v.require_clean_exit(0,log)

    def test_exact_two_unchanged_actor_units(self):
        v.validate_record(record())
        for mutate in (lambda r: r['compiled_test_units'].reverse(),
                       lambda r: r['compiled_test_units'].append(copy.deepcopy(r['compiled_test_units'][0])),
                       lambda r: r['compiled_test_units'].pop(),
                       lambda r: r['compiled_test_units'][0].update(source_sha256='0'*64)):
            changed = record(); mutate(changed)
            with self.assertRaises(ValueError): v.validate_record(changed)

    def test_production_fork_observer_and_old_object_claims_reject(self):
        for field, value in (('kind','old-selective-native-build'), ('production_units_recompiled',['MaterialInspection.cc']),
                             ('fork_patches_reapplied',True), ('links',[{},{}])):
            changed = record(); changed[field] = value
            with self.assertRaises(ValueError): v.validate_record(changed)
        for macro in (['-DBTS_BA_CONTROLLED_PROFILE_TEST=1'], ['-DBTS_BA_CONTROLLED_PROFILE_TEST=0'], ['-D','BTS_BA_CONTROLLED_PROFILE_TEST']):
            changed = record(); changed['compiled_test_units'][1]['command'] += macro
            with self.assertRaises(ValueError): v.validate_record(changed)

    def test_profile_classifier_and_publication_minimum_cannot_change(self):
        for field, value in (('profile','held-picker-burst-3s'), ('positive_minimum_publications',1)):
            changed = record(); changed[field] = value
            with self.assertRaises(ValueError): v.validate_record(changed)
        changed = record(); changed['actor_identity']['scripts/host/check-cycles-live-burst.py'] = '0'*64
        with self.assertRaises(ValueError): v.validate_record(changed)

    def test_old_evidence_cannot_be_new_production(self):
        changed = record(); changed['historical_evidence'][0]['role'] = 'production'
        with self.assertRaises(ValueError): v.validate_record(changed)

    def test_history_missing_changed_and_escape_reject(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder); file = root/'negative.json'; file.write_text('negative evidence')
            digest = hashlib.sha256(file.read_bytes()).hexdigest()
            with patch.dict(v.HISTORY, {'negative.json':digest}, clear=True):
                self.assertEqual(v.history_identity(root)[0]['role'], 'historical-only')
                file.write_text('relabeled positive')
                with self.assertRaises(ValueError): v.history_identity(root)
                file.unlink()
                with self.assertRaises(FileNotFoundError): v.history_identity(root)
            external = root.parent/(root.name+'-outside'); external.write_text('outside')
            try:
                file.symlink_to(external)
                with patch.dict(v.HISTORY, {'negative.json':hashlib.sha256(b'outside').hexdigest()}, clear=True):
                    with self.assertRaises(ValueError): v.history_identity(root)
            finally: external.unlink()

    def test_output_cannot_overwrite_artist_snapshot_or_parent(self):
        root = Path('/private-test/build'); snap = root/'snapshot'
        self.assertEqual(v.output_path(root/'fresh',snap,root),root/'fresh')
        for out in (root,root/'host',root/'host/child',snap,snap/'child',Path('/private-test')):
            with self.assertRaises(ValueError): v.output_path(out,snap,root)

    def test_real_actor_and_classification_sources_remain_exact(self):
        repo = Path(__file__).resolve().parents[2]
        self.assertEqual(v.actor_identity(repo),v.ACTOR_SHA)

    def test_missing_historical_evidence_stops_before_any_native_work(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder); out = root/'must-not-be-created'
            with patch.object(v.subprocess,'run',side_effect=AssertionError('compiler or native execution')), patch.object(v.integrated,'validate',side_effect=AssertionError('production admission reached too early')):
                with self.assertRaises(FileNotFoundError):
                    v.build(root/'missing-snapshot',Path(__file__).resolve().parents[2],out,root,2)
            self.assertFalse(out.exists())

    def test_frozen_command_preserves_flags_and_relocates_all_native_context(self):
        template=['ccache','c++','-DNDEBUG','-I/native-root/native/host','-I/fork/source','-I/build/generated','-c','/native-root/native/host/main.cc','-o','old.o','-MF','old.d','-MT','old.o']
        result=v.frozen_command(template,Path('/out/burst-source/native/tests/a.cc'),Path('/out/objects/a.o'),Path('/snapshot'),Path('/out/burst-source'),'/native-root/native',{'blender_root':'/fork','source_build':'/build'})
        self.assertIn('-DNDEBUG',result); self.assertIn('-I/out/burst-source/native/host',result)
        self.assertIn('-I/snapshot/integration-blender/source',result); self.assertIn('-I/snapshot/integration-generated/generated',result)
        self.assertEqual(result[result.index('-MF')+1],'/out/objects/a.o.d')
        self.assertEqual(result[result.index('-MT')+1],'/out/objects/a.o')
        self.assertFalse(v.controlled_define(result))

if __name__ == '__main__': unittest.main()
