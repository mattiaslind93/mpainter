"""Small displacement-profile admission negatives; mocked compiler, no GPU IO."""
import copy
import json
from pathlib import Path
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "scripts/host"))
import cycles_integrated_snapshot as integrated
import cycles_integrated_snapshot_test as base_tests
from cycles_live_manifest import required_sources, sha256, validate_manifest, require_displacement_attempt


class DisplacementAdmissionTests(unittest.TestCase):
    def fixture(self, root):
        repo, snapshot, build, fork, _ = base_tests.IntegratedSnapshotTests().fixture(root, cycles=True)
        manifest_path = snapshot / "integrated-snapshot.json"
        manifest = json.loads(manifest_path.read_text())
        # Only this tiny synthetic fixture is extended. No real receipts/objects
        # are relabelled; the real builder must validate every full-parent byte.
        original = (required_sources("displacement") - integrated.DISPLACEMENT_TEST_SOURCES) | {
            "native/host/GuiFixDemo.cc"}
        for relative in original - set(manifest["native_source_sha256"]):
            for folder in (repo, snapshot / "integration-source"):
                path = folder / relative
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text("synthetic original " + relative)
            manifest["native_source_sha256"][relative] = sha256(repo / relative)
        obj = "native/host/GuiFixDemo.cc.o"
        for folder in (build, snapshot):
            path = folder / obj; path.parent.mkdir(parents=True, exist_ok=True); path.write_text("production actor")
        commands_path = snapshot / "host-commands.json"
        commands = json.loads(commands_path.read_text())
        commands.insert(-1, f"c++ -DGUI_EXACT_FLAGS=1 -DNDEBUG -I{repo}/native/host -I{fork}/include -I{build} -o {obj} -c {repo}/native/host/GuiFixDemo.cc")
        commands[-1] = commands[-1].replace("&& :", obj + " && :")
        commands_path.write_text(json.dumps(commands))
        deps = snapshot / "native-deps.txt"
        deps.write_text(deps.read_text() + f"{obj}: #deps 1, deps mtime 1 (VALID)\n    {repo}/native/host/GuiFixDemo.cc\n")
        manifest["commands_sha256"] = sha256(commands_path)
        manifest["dependencies_sha256"] = sha256(deps)
        manifest["native_compile_units"] += 1
        manifest["link_inputs"][obj] = {"kind": "frozen", "path": obj, "sha256": sha256(snapshot / obj)}
        manifest_path.write_text(json.dumps(manifest))
        for relative in integrated.DISPLACEMENT_TEST_SOURCES | {
                "native/tests/cycles_live_displacement_plan.md", "native/tests/cycles_displacement_profile_test.py"}:
            path = repo / relative; path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text("synthetic displacement overlay " + relative)
        overlay = {relative: {"original_sha256": manifest["native_source_sha256"].get(relative), "sha256": sha256(repo / relative)}
                   for relative in integrated.DISPLACEMENT_TEST_SOURCES}
        return repo, snapshot, overlay

    def test_options_reject_nonintegrated_and_incompatible_old_modes(self):
        options = dict(displacement_profile=True, integrated_snapshot=True, udim=False, cpu_only=False,
                       sanitize_bridge=False, controlled_profile=False, burst_profile=False,
                       test_parent_manifest=None, controlled_parent_manifest=None, burst_previous_attempt=None,
                       build_only=False, run_only=False)
        integrated.require_displacement_options(SimpleNamespace(**options))
        for key in options.keys() - {"displacement_profile", "build_only", "run_only"}:
            bad = {**options, key: False if key == "integrated_snapshot" else True}
            with self.assertRaises(ValueError, msg=key):
                integrated.require_displacement_options(SimpleNamespace(**bad))
        with self.assertRaises(ValueError):
            integrated.require_displacement_options(SimpleNamespace(**{**options, "build_only": True, "run_only": True}))
        # Existing profiles are not reinterpreted as this selector.
        integrated.require_displacement_options(SimpleNamespace(displacement_profile=False))

    def test_exact_test_overlay_still_requires_full_production_attestation(self):
        with tempfile.TemporaryDirectory() as directory:
            repo, snapshot, overlay = self.fixture(Path(directory))
            integrated.validate(snapshot, repo, overlay, integrated.DISPLACEMENT_PROFILE)
            for bad in ({}, {key: value for key, value in overlay.items() if key.endswith(".cc")},
                        {**overlay, "native/host/main.cc": {}}):
                with self.assertRaises(ValueError):
                    integrated.validate(snapshot, repo, bad, integrated.DISPLACEMENT_PROFILE)
            for target in (repo / "native/host/NativeCyclesImages.cc", snapshot / "lib/libproduction.a",
                           snapshot / "integration-blender/include/live.hh", repo / "native/tests/cycles_live_displacement_artist_test.cc"):
                raw = target.read_bytes(); target.write_bytes(raw + b"changed")
                with self.assertRaises(ValueError, msg=str(target)):
                    integrated.validate(snapshot, repo, overlay, integrated.DISPLACEMENT_PROFILE)
                target.write_bytes(raw)
            # Only the named reviewed37 extension is admitted. Its presence
            # alone never attests that the36 parent was rebuilt.
            path = repo / "native/patches/0037-cycles-hosted-displacement-image-refresh.patch"
            path.write_text("--- a/include/live.hh\n+++ b/include/live.hh\n")
            with self.assertRaises(ValueError):
                integrated.validate(snapshot, repo, overlay, integrated.DISPLACEMENT_PROFILE)
            # Synthetic complete37 receipt, not real build evidence. The real
            # integration still requires every captured fork byte and input.
            manifest_path = snapshot / "integrated-snapshot.json"
            manifest = json.loads(manifest_path.read_text())
            receipt_path = snapshot / "integration-evidence/fork-series-receipt.json"
            receipt = json.loads(receipt_path.read_text())
            manifest["patch_series_sha256"] = receipt["patch_sha256"] = integrated.patch_series(repo)
            receipt_path.write_text(json.dumps(receipt))
            manifest["evidence_sha256"]["integration-evidence/fork-series-receipt.json"] = sha256(receipt_path)
            manifest_path.write_text(json.dumps(manifest))
            integrated.validate(snapshot, repo, overlay, integrated.DISPLACEMENT_PROFILE)
            for name in ("0037-a-unknown.patch", "0038-unreviewed.patch"):
                unknown = repo / "native/patches" / name
                unknown.write_text("unreviewed")
                with self.assertRaises(ValueError):
                    integrated.patch_series(repo)
                with self.assertRaises(ValueError):
                    integrated.validate(snapshot, repo, overlay, integrated.DISPLACEMENT_PROFILE)
                unknown.unlink()

    def test_three_test_units_exact_flags_and_one_actor_link_no_production_refresh(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory); repo, snapshot, overlay = self.fixture(root)
            output = root / "proof"; output.mkdir()
            original_archive = (snapshot / "lib/libproduction.a").read_bytes()
            calls = []
            def run(args, **kwargs):
                self.assertEqual(args[:3], ["nice", "-n", "19"])
                command = args[3:]; calls.append(command)
                if "-M" in command:
                    source = next(value for value in command if value.endswith(".cc"))
                    Path(command[command.index("-MF") + 1]).write_text("test: " + source + "\n")
                else:
                    Path(command[command.index("-o") + 1]).write_bytes(b"synthetic test artifact")
                return SimpleNamespace(returncode=0)
            with patch.object(integrated.subprocess, "run", side_effect=run):
                record = integrated.build_drivers(snapshot, repo, output, False, {}, displacement=True)
            self.assertEqual(len(calls), 10)  # Six scans, three TUs, one link.
            self.assertEqual(record["production_units_recompiled"], [])
            self.assertIs(record["fork_patches_reapplied"], False)
            units = {Path(unit["source"]).name: unit for unit in record["compiled_test_units"]}
            self.assertEqual(set(units), {"cycles_live_displacement_main.cc", "cycles_live_displacement_artist_test.cc", "CyclesLiveDisplacementProbe.cc"})
            self.assertIn("-DCYCLES_EXACT_ABI=1", units["CyclesLiveDisplacementProbe.cc"]["command"])
            self.assertIn("-DGUI_EXACT_FLAGS=1", units["cycles_live_displacement_artist_test.cc"]["command"])
            self.assertEqual(len(record["links"]), 1)
            link = record["links"][0]["command"]
            self.assertNotIn(str(snapshot / "native/host/GuiFixDemo.cc.o"), link)
            self.assertIn(str(snapshot / "objects/NativeCyclesImages.cc.o"), link)
            self.assertEqual((snapshot / "lib/libproduction.a").read_bytes(), original_archive)
            path, binary = output / "run-manifest.json", output / "bin/cycles-live-artist"
            check = lambda: validate_manifest(path, repo, binary, "displacement", False, integrated.DISPLACEMENT_PROFILE)
            check()
            for target in (binary, repo / "native/tests/cycles_live_displacement_plan.md", output / "build.json"):
                raw = target.read_bytes(); target.write_bytes(raw + b"changed")
                with self.assertRaises(ValueError): check()
                target.write_bytes(raw)
            with self.assertRaises(ValueError):
                validate_manifest(path, repo, binary, "displacement", False)
            with self.assertRaises(ValueError):
                validate_manifest(path, repo, binary, "128", False, integrated.DISPLACEMENT_PROFILE)

    def runtime(self, root):
        captures, events = [], []
        for epoch, stage in enumerate(("before", "edited", "undone", "redone"), 1):
            tiles = []
            for tile in (1001, 1002):
                raw = (b"a" if stage in ("before", "undone") else b"b") * (128 * 128 * 16)
                for suffix in ("gpu", "cpu"):
                    (root / f"{stage}-{tile}-{suffix}.rgba32f").write_bytes(raw)
                tiles.append({"tile": tile, "bytes": 128 * 128 * 16, "gpu_sha256": sha256(root / f"{stage}-{tile}-gpu.rgba32f"), "cpu_sha256": sha256(root / f"{stage}-{tile}-cpu.rgba32f")})
            captures.append({"stage": stage, "full_tiles": tiles,
                             "completion": {"generation": 1, "epoch": epoch, "terminal_epoch": epoch, "draw_epoch": epoch, "sample": 128, "progress": 1},
                             "geometry": {"available": True, "generation": 1, "epoch": epoch}})
            events.append({"generation": 1, "epoch": epoch, "sample": 128, "progress": 1, "status": "Rendering Done"})
            (root / (stage + "-geometry-oracle.json")).write_text(json.dumps({"checked_vertices": 24, "checked_transverse_vertices": 2145, "bound": 3e-5, "maximum_z_error": 0, "maximum_transverse_error": 2e-6}))
            for suffix in (".png", "-mesh-roi.png", "-workspace.png"):
                (root / (stage + suffix)).write_bytes(b"synthetic frame")
        (root / "painting-before.png").write_bytes(b"synthetic frame")
        report = {"profile": integrated.DISPLACEMENT_PROFILE, "captures": captures}
        trace = {"sessions": [{"generation": 1, "viewport": True, "background": False, "auto_tile": False}], "events": events}
        log = "BTS_BA_DISPLACEMENT_DONE\nBTS_SHUTDOWN clean\n"
        def write(value=report, observed=trace, text=log):
            (root / "verified.json").write_text(json.dumps(value))
            (root / "completion-trace.json").write_text(json.dumps(observed))
            (root / "run.log").write_text(text)
        write()
        return report, trace, log, write

    def test_incomplete_stale_geometry_tiles_or_unclean_attempt_never_pass(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory); report, trace, log, write = self.runtime(root)
            require_displacement_attempt(root)
            for field in ("stage", "geometry", "epoch", "draw", "tile", "mirror", "observer"):
                changed, observed = copy.deepcopy(report), copy.deepcopy(trace)
                capture = changed["captures"][1]
                if field == "stage": changed["captures"].pop()
                elif field == "geometry": capture["geometry"]["available"] = False
                elif field == "epoch": capture["geometry"]["epoch"] = 1
                elif field == "draw": capture["completion"]["draw_epoch"] = 1
                elif field == "tile": capture["full_tiles"].pop()
                elif field == "mirror": capture["full_tiles"][0]["cpu_sha256"] = "0" * 64
                else: observed["failed"] = True
                write(changed, observed)
                with self.assertRaises(ValueError, msg=field): require_displacement_attempt(root)
            for bad in (log.replace("BTS_SHUTDOWN clean", ""), log.replace("BTS_BA_DISPLACEMENT_DONE", "")):
                write(text=bad)
                with self.assertRaises(ValueError): require_displacement_attempt(root)
            write()
            oracle_path = root / "before-geometry-oracle.json"
            original_oracle = json.loads(oracle_path.read_text())
            for value in ({**original_oracle, "checked_transverse_vertices": 2144},
                          {**original_oracle, "maximum_transverse_error": 3.1e-5},
                          {**original_oracle, "maximum_transverse_error": float("nan")}):
                oracle_path.write_text(json.dumps(value))
                with self.assertRaises(ValueError): require_displacement_attempt(root)
            oracle_path.write_text(json.dumps(original_oracle))
            raw = root / "undone-1002-gpu.rgba32f"; raw.write_bytes(b"stale retained bytes")
            with self.assertRaises(ValueError): require_displacement_attempt(root)


if __name__ == "__main__":
    unittest.main()
