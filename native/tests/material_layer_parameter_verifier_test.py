"""Exercise the shipped profile's fail-closed packet check without running its CLI."""
import ast
import copy
import math
from pathlib import Path
import unittest

source = Path(__file__).resolve().parents[2] / 'scripts/host/verify-material-layering.py'
# Load the exact pure validator while leaving argument parsing and processes out.
node = next(node for node in ast.parse(source.read_text()).body
            if isinstance(node, ast.FunctionDef) and node.name == 'valid_parameter_witness')
namespace = {'math': math}
exec(compile(ast.Module(body=[node], type_ignores=[]), str(source), 'exec'), namespace)
valid = namespace['valid_parameter_witness']

class ParameterPacket(unittest.TestCase):
    def setUp(self):
        self.packet = dict(observed=True, cold=False, decoded_mask_components=393216,
            nonuniform_destination_tiles=2, fixed_tolerance=3e-5, maximum_mask_error=0,
            unique_payload_files=4, source_members_version=3,
            cancel_retired_without_project_mutation=True, original_source_hidden=False)

    def test_writer_and_source_hidden_cold(self):
        self.assertTrue(valid(self.packet, False))
        self.packet.update(cold=True, original_source_hidden=True,
                           cancel_retired_without_project_mutation=False)
        self.assertTrue(valid(self.packet, True))

    def test_missing_or_stale_packet_fails(self):
        self.assertFalse(valid({}, False))
        for field in self.packet:
            packet = copy.deepcopy(self.packet)
            del packet[field]
            if field != 'original_source_hidden':
                self.assertFalse(valid(packet, False), field)
        self.assertFalse(valid(self.packet, True))

    def test_numeric_shape_bound_and_cancel_negatives(self):
        for field, value in [('maximum_mask_error', float('nan')),
                             ('maximum_mask_error', float('inf')),
                             ('maximum_mask_error', 3e-5), ('maximum_mask_error', -1),
                             ('decoded_mask_components', 0), ('unique_payload_files', 0),
                             ('nonuniform_destination_tiles', 1),
                             ('cancel_retired_without_project_mutation', False),
                             ('source_members_version', 2),
                             ('maximum_mask_error', False), ('maximum_mask_error', True),
                             ('unique_payload_files', True), ('unique_payload_files', False),
                             ('decoded_mask_components', True), ('nonuniform_destination_tiles', True),
                             ('source_members_version', True)]:
            packet = copy.deepcopy(self.packet)
            packet[field] = value
            self.assertFalse(valid(packet, False), (field, value))

tile_node = next(node for node in ast.parse(source.read_text()).body
    if isinstance(node, ast.FunctionDef) and node.name == 'valid_parameter_distinct_tiles')
exec(compile(ast.Module(body=[tile_node], type_ignores=[]), str(source), 'exec'), namespace)
distinct_tiles = namespace['valid_parameter_distinct_tiles']

roundtrip_node = next(node for node in ast.parse(source.read_text()).body
    if isinstance(node, ast.FunctionDef) and node.name == 'valid_parameter_roundtrip_witness')
exec(compile(ast.Module(body=[roundtrip_node], type_ignores=[]), str(source), 'exec'), namespace)
roundtrip_valid = namespace['valid_parameter_roundtrip_witness']

class NativeParameterPacket(unittest.TestCase):
    def setUp(self):
        self.packet = dict(observed=True, cold=False, mask_components=98304,
            local_mask_slots=[1, 2, 3], fixed_tolerance=3e-5, mask_maximum_error=0,
            decoder_calls=7, catalog_original_rows=4, package_hidden=False,
            distinct_tile_masks=True, mask0_tile_maximum_difference=.5,
            reference_mask0_tile_maximum_difference=.5,
            actual_full_document_publication=True, one_history_commit=True, catalog_originals_once=True,
            fresh_shader_assignment=True, native_shader_parameters=True,
            unrelated_sets_unchanged=True, stale_result_rejected=True,
            owner_admitted_before_worker_decode=True, ordinary_layer_undo_retains_shader=True,
            catalog_metadata_preflight=True, missing_filename_rejected=True,
            undo_exact_document_and_library=True, redo_exact_document_and_library=True,
            original_source_hidden=True)

    def test_native_writer_and_package_hidden_cold(self):
        self.assertTrue(roundtrip_valid(self.packet, False))
        self.packet.update(cold=True, package_hidden=True)
        self.assertTrue(roundtrip_valid(self.packet, True))

    def test_old_or_incomplete_actor_never_passes(self):
        self.assertFalse(roundtrip_valid({}, False))
        for key in self.packet:
            packet = copy.deepcopy(self.packet)
            del packet[key]
            self.assertFalse(roundtrip_valid(packet, False), key)
        self.assertFalse(roundtrip_valid(self.packet, True))

    def test_numeric_identity_and_history_failures(self):
        for field, value in [('mask_components', 0), ('mask_maximum_error', float('nan')),
                             ('mask_maximum_error', float('inf')), ('mask_maximum_error', 3e-5),
                             ('mask_maximum_error', -1), ('local_mask_slots', [0, 1, 2]),
                             ('decoder_calls', 0), ('catalog_original_rows', 0),
                             ('mask_maximum_error', False), ('mask_maximum_error', True),
                             ('decoder_calls', True), ('decoder_calls', False),
                             ('catalog_original_rows', True), ('catalog_original_rows', False),
                             ('mask_components', True), ('local_mask_slots', [True, 2, 3]),
                             ('distinct_tile_masks', False), ('mask0_tile_maximum_difference', .1),
                             ('reference_mask0_tile_maximum_difference', 0),
                             ('mask0_tile_maximum_difference', True),
                             ('reference_mask0_tile_maximum_difference', float('nan')),
                             ('one_history_commit', False), ('native_shader_parameters', False),
                             ('catalog_metadata_preflight', False), ('missing_filename_rejected', False),
                             ('ordinary_layer_undo_retains_shader', False),
                             ('undo_exact_document_and_library', False), ('redo_exact_document_and_library', False)]:
            packet = copy.deepcopy(self.packet)
            packet[field] = value
            self.assertFalse(roundtrip_valid(packet, False), (field, value))

class DistinctTilePacket(unittest.TestCase):
    def test_actual_asymmetric_reference_is_required(self):
        packet = dict(distinct_tile_masks=True, mask0_tile_maximum_difference=.5,
                      reference_mask0_tile_maximum_difference=.4)
        self.assertTrue(distinct_tiles(packet))
        for key in packet:
            missing = dict(packet)
            del missing[key]
            self.assertFalse(distinct_tiles(missing), key)
        for key in ['mask0_tile_maximum_difference', 'reference_mask0_tile_maximum_difference']:
            for value in [False, True, 0, .1, 1.01, float('nan'), float('inf')]:
                self.assertFalse(distinct_tiles(dict(packet, **{key: value})), (key, value))

import_node = next(node for node in ast.parse(source.read_text()).body
    if isinstance(node, ast.FunctionDef) and node.name == 'valid_parameter_import_witness')
exec(compile(ast.Module(body=[import_node], type_ignores=[]), str(source), 'exec'), namespace)
import_valid = namespace['valid_parameter_import_witness']

class ProductImportPacket(unittest.TestCase):
    def setUp(self):
        self.packet = dict(observed=True, cold=False, mask_components=98304,
            local_mask_slots=[1, 2, 3], fixed_tolerance=3e-5, mask_maximum_error=0,
            catalog_original_rows=4, package_hidden=False, distinct_tile_masks=True,
            mask0_tile_maximum_difference=.5, reference_mask0_tile_maximum_difference=.5,
            actual_import_button_and_directory=True, folder_cancel_unchanged=True,
            actual_native_surface_rejected_before_worker=True, worker_cancel_retired_unchanged=True,
            preparing_cancel_releases_owner_unchanged=True, completed_legacy_without_cancel=True,
            actual_full_document_publication=True, one_history_commit=True, catalog_originals_once=True,
            fresh_shader_assignment=True, native_shader_parameters=True, unrelated_sets_unchanged=True,
            stale_result_rejected=True, ordinary_layer_undo_retains_shader=True,
            undo_exact_document_and_library=True, redo_exact_document_and_library=True,
            original_source_hidden=True)

    def test_actual_import_and_hidden_cold(self):
        self.assertTrue(import_valid(self.packet, False))
        self.packet.update(cold=True, package_hidden=True)
        self.assertTrue(import_valid(self.packet, True))

    def test_missing_ui_lifecycle_or_old_diagnostic_packet_fails(self):
        self.assertFalse(import_valid({}, False))
        for key in self.packet:
            packet = dict(self.packet)
            del packet[key]
            self.assertFalse(import_valid(packet, False), key)
        native = NativeParameterPacket()
        native.setUp()
        self.assertFalse(import_valid(native.packet, False))

    def test_strict_numeric_and_lifecycle_negatives(self):
        for field, value in [('mask_components', True), ('mask_components', 0),
                             ('mask_maximum_error', False), ('mask_maximum_error', 3e-5),
                             ('mask_maximum_error', float('nan')), ('catalog_original_rows', True),
                             ('catalog_original_rows', 3), ('local_mask_slots', [True, 2, 3]),
                             ('preparing_cancel_releases_owner_unchanged', False),
                             ('completed_legacy_without_cancel', False),
                             ('actual_import_button_and_directory', 1)]:
            self.assertFalse(import_valid(dict(self.packet, **{field: value}), False), (field, value))

color_node = next(node for node in ast.parse(source.read_text()).body
    if isinstance(node, ast.FunctionDef) and node.name == 'valid_color_udim_witness')
exec(compile(ast.Module(body=[color_node], type_ignores=[]), str(source), 'exec'), namespace)
color_valid = namespace['valid_color_udim_witness']

class LinearColorUdimPacket(unittest.TestCase):
    def packet(self, cold=False):
        probe = dict(tile=1001,xy=[13,19],actual_color_metal=[-.125,.25,1.25,0],
            independent=[-.125,.25,1.25,0],observed_masks=[.25,.5,.125])
        def row(stage,stock=False,rec709=False):
            return dict(stage=stage,stock=stock,private_rec709_matrix=rec709,components=128,
                finite=True,nonfinite_components=0,fixed_tolerance=3e-5,maximum_error=0,
                mask_plane_maximum_error=0,restoration_error=0,nonfinite_witness={},
                worst=dict(actual_rgba=[-.125,.25,1.25,1],absolute_error=0),
                probes=[copy.deepcopy(probe) for _ in range(32)])
        labels = ['source-package-hidden-cold'] if cold else ['file-shelf','srt','undo','redo','reconstructed','reconstruction-redo']
        stages = ['cold-resaved-stock'] if cold else ['writer-stock','reconstructed-stock']
        rows=[row(label) for label in labels]
        for label in stages:
            rows.extend([row(label,True),row('stock-private-rec709',True,True)])
        return dict(native_run_pass=True,cold=cold,captures=rows,
            stock=[dict(stage=label,original_payloads=2,runtime_aliases=0,native_restore_called=False,
                maximum_error=0,private709_maximum_error=0) for label in stages],
            package=dict(version=2,source_members_version=4,mask_components=98304,mask_maximum_error=0,payload_files=6),
            reconstructed_masks=dict(components=98304,maximum_error=0,fixed_tolerance=3e-5),
            reconstruction=dict(native_target_before_decode=True,stale_publication_rejected=True,
                linear_member_admissions=3,decoder_calls=7,one_history_commit=True,undo_redo_exact=True),
            actual_distinct_member_tags=True,originals_hidden=True,package_hidden=cold,
            shared_inputs=dict(unique_live_images=4,unique_live_textures=4),
            shared_two_sets=dict(unique_live_images=4,unique_live_textures=4),
            cancel_worker_pending_observed=True,cancel_fingerprint_unchanged=True,file_apply_one_undo=True,srt_stable_handles_counts=True,
            missing_footprint_atomic=True,export_cancel_unchanged=True,
            srt_upload_before=dict(calls=12,bytes=512),srt_upload_after=dict(calls=12,bytes=512))

    def test_exact_writer_and_cold_packets(self):
        self.assertTrue(color_valid(self.packet(),False))
        self.assertTrue(color_valid(self.packet(True),True))

    def test_missing_numeric_stock_and_native_admission_fail(self):
        for path in [('captures',),('stock',),('reconstruction',),('package',),('shared_two_sets',)]:
            packet=self.packet();del packet[path[0]]
            self.assertFalse(color_valid(packet,False),path)
        packet=self.packet();packet['reconstruction']['linear_member_admissions']=0
        self.assertFalse(color_valid(packet,False))
        packet=self.packet();packet['stock'][0]['runtime_aliases']=1
        self.assertFalse(color_valid(packet,False))
        packet=self.packet();packet['package']['source_members_version']=3
        self.assertFalse(color_valid(packet,False))

    def test_nonfinite_and_unchanged_fixed_bound_fail_closed(self):
        for value in (float('nan'),float('inf'),3e-5,-1,True):
            packet=self.packet();packet['captures'][0]['maximum_error']=value
            self.assertFalse(color_valid(packet,False),value)
        packet=self.packet();packet['captures'][0]['probes'][0]['actual_color_metal'][0]=float('nan')
        self.assertFalse(color_valid(packet,False))
        packet=self.packet();packet['captures'][0]['finite']=False
        self.assertFalse(color_valid(packet,False))

    def test_pending_cancel_sharing_and_hidden_inputs_are_required(self):
        for field in ('cancel_worker_pending_observed','cancel_fingerprint_unchanged','file_apply_one_undo','srt_stable_handles_counts',
                      'missing_footprint_atomic','export_cancel_unchanged','actual_distinct_member_tags'):
            packet=self.packet();packet[field]=False;self.assertFalse(color_valid(packet,False),field)
        packet=self.packet(True);packet['package_hidden']=False
        self.assertFalse(color_valid(packet,True))
        packet=self.packet();packet['srt_upload_after']['bytes']+=16
        self.assertFalse(color_valid(packet,False))

class LinearColorProductImportPacket(unittest.TestCase):
    packet = LinearColorUdimPacket.packet
    def product(self, cold=False):
        packet=self.packet(cold);packet['product_import_mode']=True
        packet['reconstruction'].pop('decoder_calls');packet['reconstruction'].pop('linear_member_admissions')
        packet['product_import']={key:True for key in ('folder_cancel_unchanged','native_surface_before_worker',
            'worker_cancel_retired_unchanged','stale_result_rejected','actual_import_button_and_directory',
            'one_history_commit','catalog_originals_once')}
        return packet

    def test_real_product_mode_and_hidden_cold(self):
        for cold in (False,True):
            self.assertTrue(color_valid(self.product(cold),cold,True))
            self.assertFalse(color_valid(self.product(cold),cold))
            self.assertFalse(color_valid(self.packet(cold),cold,True))

    def test_every_product_boundary_required_and_same_numeric_bounds(self):
        packet=self.product()
        for key in packet['product_import']:
            changed=copy.deepcopy(packet);del changed['product_import'][key]
            self.assertFalse(color_valid(changed,False,True),key)
        packet['captures'][0]['maximum_error']=3e-5
        self.assertFalse(color_valid(packet,False,True))

if __name__ == '__main__':
    unittest.main()
