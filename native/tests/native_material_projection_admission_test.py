"""Admission negatives only; no native fixture, shader or UI runtime claim."""
import copy
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

root = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location(
    'projection_admission', root / 'scripts/host/native_material_projection_admission.py')
admission = importlib.util.module_from_spec(spec)
spec.loader.exec_module(admission)

class Admission(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.directory = Path(self.temporary.name)
        self.addCleanup(self.temporary.cleanup)
        for name in admission.FILES:
            (self.directory / name).write_bytes(b'opaque admission fixture only')
        self.fixture = {
            'format': 'bts-native-projection-fixture', 'version': 1,
            'generator_sha256': 'a' * 64,
            'source_blend_sha256': admission.sha(self.directory / 'owned-projection.blend'),
            'uniform_packed_image': {'filepath': '//owned-uniform.png',
                                     'source': 'FILE', 'verified_reopen': True, 'packed_paths': ['//owned-uniform.png'],
                                     'packed': True, 'packed_bytes': 42},
            'tiles': [1001, 1002, 1011], 'owner_names': ['BJ Owner A', 'BJ Owner B'],
            'native_materials': ['BJ Native UV', 'BJ Native Object', 'BJ Native Generated'],
            'uniform_material': 'BJ Uniform bitmap'}
        self.write()

    def write(self):
        (self.directory / 'fixture.json').write_text(json.dumps(self.fixture))

    def validate(self, kind='UV'):
        return admission.validate_fixture(self.directory, kind, 'a' * 64)

    def test_complete_declared_fixture_all_kinds(self):
        for kind in admission.KINDS:
            _, hashes = self.validate(kind)
            self.assertEqual(set(hashes), set(admission.FILES))

    def test_missing_source(self):
        (self.directory / 'owned-height.exr').unlink()
        with self.assertRaisesRegex(ValueError, 'Missing required fixture file'):
            self.validate()

    def test_missing_manifest(self):
        (self.directory / 'fixture.json').unlink()
        with self.assertRaisesRegex(ValueError, 'Missing required fixture file'):
            self.validate()

    def test_wrong_manifest(self):
        self.fixture['format'] = 'unrelated'
        self.write()
        with self.assertRaisesRegex(ValueError, 'Wrong projection fixture manifest'):
            self.validate()

    def test_wrong_kind(self):
        with self.assertRaisesRegex(ValueError, 'Unsupported projection fixture kind'):
            self.validate('Position')
        self.fixture['native_materials'].remove('BJ Native Generated')
        self.write()
        with self.assertRaisesRegex(ValueError, 'Requested kind is absent'):
            self.validate('Generated')

    def test_absolute_packed_image_reference_rejected(self):
        self.fixture['uniform_packed_image']['filepath'] = str(self.directory / 'owned-uniform.png')
        self.write()
        with self.assertRaisesRegex(ValueError, 'required relative filename'):
            self.validate()

    def test_absolute_packed_member_reference_rejected(self):
        self.fixture['uniform_packed_image']['packed_paths'] = [str(self.directory / 'owned-uniform.png')]
        self.write()
        with self.assertRaisesRegex(ValueError, 'required relative filename'):
            self.validate()

    def test_generated_image_rejected(self):
        self.fixture['uniform_packed_image']['source'] = 'GENERATED'
        self.write()
        with self.assertRaisesRegex(ValueError, 'required relative filename'):
            self.validate()

    def test_missing_serialized_path_readback_rejected(self):
        self.fixture['uniform_packed_image'].pop('verified_reopen')
        self.write()
        with self.assertRaisesRegex(ValueError, 'required relative filename'):
            self.validate()

    def test_unpacked_image_rejected(self):
        self.fixture['uniform_packed_image']['packed'] = False
        self.write()
        with self.assertRaisesRegex(ValueError, 'required relative filename'):
            self.validate()

    def test_changed_generator_rejected(self):
        self.fixture['generator_sha256'] = 'b' * 64
        self.write()
        with self.assertRaisesRegex(ValueError, 'generator source hash differs'):
            self.validate()

    def test_changed_blend_bytes_rejected(self):
        (self.directory / 'owned-projection.blend').write_bytes(b'changed')
        with self.assertRaisesRegex(ValueError, 'blend bytes differ'):
            self.validate()

    def test_single_tile_declaration_rejected(self):
        self.fixture['tiles'] = [1001]
        self.write()
        with self.assertRaisesRegex(ValueError, 'tile/owner membership'):
            self.validate()

    def test_wrong_or_incomplete_predecessor(self):
        receipt = {'actual_exit': 0, 'clean_exit': True, 'phase': 'warm', 'kind': 'UV',
                   'binary_sha256': 'binary', 'source_hashes': {'source': 'hash'}}
        proof = {'failure': '', 'phase': 'warm', 'kind': 'UV',
                 'checkpoints': [{'policy': p, 'mode': m} for p in range(2) for m in range(7)]}
        admission.validate_predecessor(receipt, proof, 'UV', 'binary', {'source': 'hash'})
        for key, value in [('actual_exit', 1), ('clean_exit', False), ('kind', 'uniform'),
                           ('binary_sha256', 'other')]:
            wrong = copy.deepcopy(receipt)
            wrong[key] = value
            with self.assertRaises(ValueError):
                admission.validate_predecessor(wrong, proof, 'UV', 'binary', {'source': 'hash'})
        proof['checkpoints'][-1] = proof['checkpoints'][0]
        with self.assertRaisesRegex(ValueError, 'complete unique policy/mode matrix'):
            admission.validate_predecessor(receipt, proof, 'UV', 'binary', {'source': 'hash'})

    def test_cold_actual_frames_and_data_first(self):
        proof = {'checkpoints': [{'normal_eevee_frames': 8,
                                 'data': {'explicit_eevee_frames_before_validation': 0}}
                                for _ in range(14)], 'normal_eevee_frames': 112}
        proof['checkpoints'][3]['normal_eevee_frames'] += 5
        proof['normal_eevee_frames'] += 5
        admission.validate_cold_frame_progression(proof)
        for mutate in [lambda p: p.update(normal_eevee_frames=112),
                       lambda p: p['checkpoints'][0].update(normal_eevee_frames=7),
                       lambda p: p['checkpoints'][0]['data'].update(explicit_eevee_frames_before_validation=1)]:
            invalid = copy.deepcopy(proof)
            mutate(invalid)
            with self.assertRaises(ValueError):
                admission.validate_cold_frame_progression(invalid)

    def test_cold_helper_keeps_data_before_draw_and_advances_pending_frames(self):
        source = (root / 'native/host/NativeMaterialProjectionColdProof.hh').read_text()
        self.assertLess(source.index('data_file.close();'), source.index('host.draw('))
        pending = source[source.index('while(source_loader.pending()||host.texture_updates_pending())'):source.index('int components=0;')]
        self.assertIn('host.draw(', pending)
        self.assertLess(source.index('data_file.close();'), source.index('source_loader.ensure('))
        self.assertIn('NativeMaterialSourceLoader source_loader', source)
        self.assertIn('const auto resources=host.project_resources();', source)
        self.assertIn('source.ready&&host.has_native_material(id)', source)
        self.assertIn('require_hydration();', pending)
        self.assertLess(pending.index('host.draw('), pending.index('host.flush_texture_updates()'))
        self.assertIn('std::chrono::seconds(120)', source)
        self.assertIn('std::chrono::steady_clock::now()<deadline', pending)
        self.assertIn('"normal_eevee_frames",checkpoint_frames', source)

if __name__ == '__main__':
    unittest.main()
