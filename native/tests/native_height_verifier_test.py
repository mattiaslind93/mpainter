"""CPU-only admission negatives; no compiler, native process or GPU is started."""
import copy
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "scripts/host"))
import native_height_verifier as height


class HeightAdmission(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        root = Path(self.temp.name)
        self.repo, self.snapshot, self.out = root / "repo", root / "snapshot", root / "out"
        for directory in (self.repo, self.snapshot, self.out / "bin", self.out / "objects"):
            directory.mkdir(parents=True, exist_ok=True)
        for name in height.RUNNERS:
            self.file(self.repo / name, name)
        self.file(self.snapshot / "integrated-snapshot.json", "attested")
        self.file(self.out / "test-headers.json", "{}")
        self.commands = [": && c++ /prod/native/host/main.cc.o /prod/native/host/GuiFixDemo.cc.o /prod/lib.a -o /prod/bin/bts-host && :"]
        self.file(self.snapshot / "host-commands.json", json.dumps(self.commands))
        self.admitted = {"native_source_head": "production-head", "source_build": "/prod", "linker_scripts": {},
                         "link_inputs": {value: {"kind": "frozen", "path": "inputs/" + Path(value).name}
                                         for value in ("/prod/native/host/main.cc.o", "/prod/native/host/GuiFixDemo.cc.o", "/prod/lib.a")}}
        self.record = {"schema": 1, "profile": height.PROFILE, "snapshot": str(self.snapshot),
                       "snapshot_manifest_sha256": height.sha256(self.snapshot / "integrated-snapshot.json"),
                       "built_source_head": "production-head", "runner_sha256": height.source_identity(self.repo),
                       "production_units_recompiled": [], "fork_patches_reapplied": False,
                       "header_manifest_sha256": height.sha256(self.out / "test-headers.json"),
                       "compiled_test_units": [], "links": []}
        for actor, (name, _) in height.ACTORS.items():
            source = self.file(self.repo / "native/tests" / name, actor)
            obj = self.file(self.out / "objects" / (actor + ".o"), "object-" + actor)
            binary = self.file(self.out / "bin" / ("height-policy-" + actor), "binary-" + actor)
            command, replaced = height.actor_link(self.commands, self.admitted, self.snapshot, self.out, actor, obj)
            dep = self.file(self.out / (actor + ".frozen.d"), "frozen test dependency scan")
            self.record["compiled_test_units"].append({"actor": actor, "source": str(source.relative_to(self.repo)),
                       "source_sha256": height.sha256(source), "object": str(obj), "object_sha256": height.sha256(obj),
                       "scan_sha256": height.sha256(dep), "command": ["c++", "-c", str(self.snapshot / "integration-source/native/tests" / name), "-o", str(obj)]})
            self.record["links"].append({"actor": actor, "binary": str(binary), "sha256": height.sha256(binary),
                                         "command": command, "replaced_production_object": replaced})
        self.persist()
        self.addCleanup(patch.stopall)
        patch.object(height.integration, "validate", return_value=self.admitted).start()
        patch.object(height.integration, "validate_test_headers").start()

    def file(self, path, text):
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text)
        return path

    def persist(self):
        height.write_json(self.out / "build.json", self.record)
        height.write_json(self.out / "run-manifest.json", {"schema": 1, "profile": height.PROFILE,
                           "build_sha256": height.sha256(self.out / "build.json")})

    def validate(self):
        return height.validate_build(self.snapshot, self.repo, self.out)

    def test_exact_two_tu_mapping_and_full_parent(self):
        self.validate()
        owner, artist = self.record["links"]
        self.assertIn(str(self.snapshot / "inputs/main.cc.o"), artist["command"])
        self.assertNotIn(str(self.snapshot / "inputs/GuiFixDemo.cc.o"), artist["command"])
        self.assertIn(str(self.snapshot / "inputs/GuiFixDemo.cc.o"), owner["command"])
        with patch.object(height.integration, "validate", side_effect=ValueError("incomplete full closure")):
            with self.assertRaises(ValueError):
                self.validate()

    def test_stale_sources_binary_object_headers_parent_runner(self):
        paths = [self.repo / self.record["compiled_test_units"][0]["source"],
                 Path(self.record["compiled_test_units"][0]["object"]),
                 Path(self.record["links"][1]["binary"]), self.out / "test-headers.json",
                 self.snapshot / "integrated-snapshot.json", self.repo / height.RUNNERS[0], self.out / "artist.frozen.d"]
        for path in paths:
            with self.subTest(path=path):
                original = path.read_bytes()
                path.write_bytes(original + b"changed")
                with self.assertRaises(ValueError):
                    self.validate()
                path.write_bytes(original)

    def test_production_refresh_patch_or_extra_tu_rejected(self):
        for field, value in (("production_units_recompiled", ["host.cc"]), ("fork_patches_reapplied", True),
                             ("compiled_test_units", self.record["compiled_test_units"] * 2)):
            before = copy.deepcopy(self.record)
            self.record[field] = value
            self.persist()
            with self.assertRaises(ValueError):
                self.validate()
            self.record = before
        self.persist()

    def test_wrong_artist_replacement_even_with_rehashed_manifest(self):
        self.record["links"][1]["command"] = self.record["links"][0]["command"]
        self.persist()
        with self.assertRaisesRegex(ValueError, "replacement"):
            self.validate()

    def test_fixture_manifest_bound_to_build_and_all_files(self):
        original = self.file(self.out / "own.blend.held", "private fixture")
        manifest = self.out / "fixtures.json"
        record = {"schema": 1, "profile": height.PROFILE, "build_sha256": "build", "native": {str(original): height.sha256(original)}, "artist": {}}
        height.write_json(manifest, record)
        self.assertEqual(height.validate_fixtures(manifest, "build"), record)
        with self.assertRaises(ValueError):
            height.validate_fixtures(manifest, "different-build")
        original.write_text("changed")
        with self.assertRaises(ValueError):
            height.validate_fixtures(manifest, "build")

    def test_strict_native_transport_spans_and_no_external_images(self):
        package, directory = self.out / "own.btsnative", self.out / "fixture"
        directory.mkdir()
        manifest = json.dumps({"version": 1, "images": []}).encode()
        payload = b"BLENDER-private-test-bytes"
        data = b"MFPNAT1\n" + struct.pack(">QQ", len(manifest), len(payload)) + manifest + payload
        package.write_bytes(data)
        self.assertEqual(height.materialize(package, directory).read_bytes(), payload)
        with self.assertRaises(ValueError):
            height.materialize(package, directory)
        for broken in (data[:-1], b"x" + data[1:], data + b"extra"):
            package.write_bytes(broken)
            with self.assertRaises(ValueError):
                height.materialize(package, directory)
        wrong = json.dumps({"version": 1, "images": [{"path": "foreign"}]}).encode()
        package.write_bytes(b"MFPNAT1\n" + struct.pack(">QQ", len(wrong), len(payload)) + wrong + payload)
        with self.assertRaises(ValueError):
            height.materialize(package, directory)

    def test_include_prefix_and_finite_oracle(self):
        mapping = [("/fork", "/frozen/fork"), ("/fork/bts/native", "/frozen/native")]
        self.assertEqual(height.relocate_include("-I/fork/bts/native/host", mapping), "-I/frozen/native/host")
        self.assertEqual(height.relocate_include("/fork/intern", mapping), "/frozen/fork/intern")
        self.assertEqual(height.relocate_include("-I/fork-other", mapping), "-I/fork-other")
        for value in (float("nan"), float("inf"), -1, 3e-5, True):
            self.assertFalse(height.finite_bound(value, 3e-5))
        self.assertTrue(height.finite_bound(0, 3e-5))


class NativeRenderingProof(unittest.TestCase):
    def proof(self):
        captures = []
        for stage, value, history in (("before", .25, 2), ("edited", .75, 3), ("undone", .25, 2), ("redone", .75, 3)):
            packets = [{"tile": tile, "channel": channel, "xy": [64, 64],
                        "gpu": [value, value, value, 1], "cpu": [value, value, value, 1],
                        "expected": [value, value, value, 1], "actual_gpu_format": "SFLOAT_32_32_32_32",
                        "height_footprint_xywh": [63, 63, 3, 3] if channel == 4 else [],
                        "height_footprint_rgba": [[value, value, value, 1] for _ in range(9)] if channel == 4 else []}
                       for tile in (1001, 1002) for channel in (2, 4)]
            captures.append({"stage": stage, "expected_scalar": value, "fixed_bound": 3e-5,
                             "render_state": {"active": True, "image_mirror_paused": False, "status": "Rendering Done"}, "render_frames": 10 + history,
                             "undo_count": history, "handles": [["image", "texture"]],
                             "native": {"height_policy": "authored-height-raw-v1", "channels": [2, 4],
                                        "parameters": [{"name": "Scalar", "value": [value]}]},
                             "samples": packets, "cycles_graph_count": "7", "cycles_svm_count": "1",
                             "hidden_normal_cpu_both_tiles": [{"tile": tile, "xy": [64, 64], "rgba": [.5, .5, 1, 1]}
                                                              for tile in (1001, 1002)],
                             "hidden_normal_gpu_tile1001": [.5, .5, 1, 1]})
        return {"production_main_callbacks": True, "raw_activated_in_painting": True,
                "rendering_active_all_edits": True, "source_catalog_unchanged": True,
                "one_history_gesture": True, "rendered_roi_undo_exact": True, "rendered_roi_redo_exact": True,
                "gpu_scalar_tiles": [1001, 1002], "gpu_scalar_channels": [2, 4], "scalar_precision": "L32F",
                "cpu_interactive_composition": 0, "native_program_compile_count_measured": False,
                "native_bump_response_equivalence": False, "performance_claim": False,
                "hidden_normal_gpu_scope": "tile1001 at64,64 only",
                "hidden_normal_cpu_scope": "center64,64 both128UDIM mirrors; constant covered3x3 H footprint",
                "captures": captures}

    def test_exact_scalar_packets_and_qualified_normal_scope(self):
        self.assertEqual(height.validate_rendering_proof(self.proof()),
                         {"components": 64, "maximum_scalar_error": 0, "fixed_bound": 3e-5})

    def test_missing_duplicate_stale_nonfinite_and_relaxed_bound_rejected(self):
        def row(proof):
            return proof["captures"][1]
        mutations = [
            lambda p: p["captures"].pop(),
            lambda p: row(p)["samples"].pop(),
            lambda p: row(p)["samples"][0].update(tile=1002),
            lambda p: row(p)["samples"][0].update(gpu=[.25, .25, .25, 1]),
            lambda p: row(p)["samples"][0]["gpu"].__setitem__(0, float("nan")),
            lambda p: row(p)["samples"][0]["cpu"].__setitem__(0, float("inf")),
            lambda p: row(p)["samples"][0]["gpu"].__setitem__(0, True),
            lambda p: row(p)["samples"][0].update(gpu=[]),
            lambda p: row(p)["samples"][0]["cpu"].__setitem__(0, .7500001),
            lambda p: row(p).update(fixed_bound=1e-3),
            lambda p: row(p)["samples"][0].update(actual_gpu_format="SFLOAT_16_16_16_16"),
            lambda p: row(p)["samples"][1].update(height_footprint_rgba=[]),
            lambda p: row(p)["samples"][1]["height_footprint_rgba"][0].__setitem__(3, 0),
            lambda p: row(p)["samples"][1]["height_footprint_rgba"][0].__setitem__(0, float("nan")),
            lambda p: row(p)["samples"][1].update(height_footprint_xywh=[0, 0, 3, 3]),
            lambda p: row(p)["render_state"].update(image_mirror_paused=True),
            lambda p: row(p)["render_state"].update(status="Rendering"),
        ]
        for mutate in mutations:
            proof = self.proof()
            mutate(proof)
            with self.subTest(mutation=mutate), self.assertRaises(ValueError):
                height.validate_rendering_proof(proof)

    def test_history_handles_source_and_normal_changes_rejected(self):
        mutations = [
            lambda p: p.update(source_catalog_unchanged=False),
            lambda p: p.update(rendered_roi_undo_exact=False),
            lambda p: p.update(hidden_normal_gpu_scope="both UDIM GPU full images"),
            lambda p: p.update(native_program_compile_count_measured=True),
            lambda p: p["captures"][1].update(undo_count=4),
            lambda p: p["captures"][1].update(handles=[]),
            lambda p: p["captures"][1].update(cycles_svm_count="2"),
            lambda p: p["captures"][1]["native"].update(resource="different source"),
            lambda p: p["captures"][1]["native"]["channels"].append(3),
            lambda p: p["captures"][2]["native"].update(resource="different source"),
            lambda p: p["captures"][2]["hidden_normal_cpu_both_tiles"][1]["rgba"].__setitem__(0, .6),
            lambda p: p["captures"][2]["hidden_normal_gpu_tile1001"].__setitem__(0, float("nan")),
        ]
        for mutate in mutations:
            proof = self.proof()
            mutate(proof)
            with self.subTest(mutation=mutate), self.assertRaises(ValueError):
                height.validate_rendering_proof(proof)


if __name__ == "__main__":
    unittest.main()
