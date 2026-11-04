"""CPU-only stale-proof admission checks; no Blender, GPU or large files."""
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from cycles_integrated_snapshot_test import IntegratedSnapshotTests

MODULE = Path(__file__).resolve().parents[2] / "scripts/host/cycles_live_manifest.py"
spec = importlib.util.spec_from_file_location("cycles_live_manifest", MODULE)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class ManifestTests(unittest.TestCase):
    def test_controlled_profile_requires_real_epochs_full_tiles_and_noop(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            stages = ["cycles-before", "cycles-noop-control", "cycles-color-edited", "cycles-color-undone", "cycles-color-redone", "cycles-camera-only", "cycles-reentered-latest"]
            captures, events = [], []
            for index, name in enumerate(stages):
                generation, epoch = (1, index + 1) if index < 6 else (2, 1)
                sequence = epoch * 2
                ns = (index + 1) * 100
                events.extend([{"generation": generation, "epoch": epoch, "sequence": sequence - 1, "sample": 0, "progress": 0, "status": "Rendering", "steady_ns": ns - 10},
                               {"generation": generation, "epoch": epoch, "sequence": sequence, "sample": 128, "progress": 1, "status": "Rendering Done", "steady_ns": ns}])
                color = "b" if index in (2, 4, 5) else "c" if index == 6 else "a"
                tiles = [{"channel": channel, "tile": tile, "bytes": str(2048 * 2048 * 16), "gpu_sha256": (color if channel == 0 else "a") * 64, "cpu_sha256": (color if channel == 0 else "a") * 64} for channel in (0, 2) for tile in (1001, 1002)]
                captures.append({"stage": name, "document": {"texture_sets": ["fixture"]}, "controlled_settings": {"adaptive": False, "animated_seed": False, "pixel_jitter": False, "denoise": False, "samples": 128, "frame": 1, "seed": 0,"color_management":{"view_transform":"Standard","look":"None","exposure":0,"gamma":1,"display_device":"sRGB","dither":0,"working_space":"ACEScg","working_space_interop_id":"lin_ap1_scene"}}, "full_tile_hashes": tiles,
                                 "actual_completion": {"generation": str(generation), "epoch": str(epoch), "terminal_epoch": str(epoch), "sequence": str(sequence), "sample": 128, "progress": 1, "status": "Rendering Done", "draw_generation": str(generation), "draw_epoch": str(epoch), "paint_begin_ns": str(ns + 1), "paint_done_ns": str(ns + 2)}})
            report = {"profile": "controlled-fixed-sampling-v1", "captures": captures}
            log = "BTS_BA_NOOP_RELOAD_BEGIN\nBTS_CYCLES_IMAGE_RELOAD texture_id=8\nBTS_CYCLES_IMAGE_RELOAD texture_id=9\nBTS_CYCLES_RELOAD images=2 owner_ms=1\nBTS_BA_NOOP_RELOAD_END\nBTS_BA_IDLE_BEGIN\nBTS_BA_IDLE_END\nBTS_BA_CAMERA_BEGIN\nBTS_BA_CAMERA_END\nBTS_BA_UDIM_DONE\n"
            def write(value=report, trace=events, text=log):
                (root / "cycles-udim.json").write_text(json.dumps(value))
                (root / "completion-trace.json").write_text(json.dumps({"events": trace,"sessions":[{"generation":g,"viewport":True,"background":False,"auto_tile":False} for g in (1,2)]}))
                (root / "run.log").write_text(text)
            write()
            module.require_completed_attempt(root, True, True)
            with self.assertRaises(ValueError):
                module.require_completed_attempt(root, True)
            for change in ("adaptive", "draw", "sequence", "hash", "undo", "redo", "document", "cm", "missing-noop", "fake-reset"):
                changed, trace = json.loads(json.dumps(report)), json.loads(json.dumps(events))
                if change == "adaptive": changed["captures"][0]["controlled_settings"]["adaptive"] = True
                elif change == "draw": changed["captures"][0]["actual_completion"]["paint_begin_ns"] = "1"
                elif change == "sequence": changed["captures"][0]["actual_completion"]["sequence"] = "999"
                elif change == "hash": changed["captures"][0]["full_tile_hashes"][0]["cpu_sha256"] = "b" * 64
                elif change == "undo":
                    changed["captures"][3]["full_tile_hashes"][0]["gpu_sha256"] = "b" * 64
                    changed["captures"][3]["full_tile_hashes"][0]["cpu_sha256"] = "b" * 64
                elif change == "redo":
                    changed["captures"][4]["full_tile_hashes"][0]["gpu_sha256"] = "a" * 64
                    changed["captures"][4]["full_tile_hashes"][0]["cpu_sha256"] = "a" * 64
                elif change == "document": changed["captures"][1]["document"]["changed"] = True
                elif change == "cm": changed["captures"][1]["controlled_settings"]["color_management"]["exposure"] = 1
                elif change == "missing-noop": changed["captures"].pop(1)
                else: trace[2]["sample"] = 128
                write(changed, trace)
                with self.assertRaises(ValueError, msg=change):
                    module.require_completed_attempt(root, True, True)
            for changed_log in (log.replace("texture_id=9", "texture_id=8"), log.replace("BTS_BA_NOOP_RELOAD_END", "BTS_CYCLES_MIRROR_CAPTURE\nBTS_BA_NOOP_RELOAD_END")):
                write(text=changed_log)
                with self.assertRaises(ValueError):
                    module.require_completed_attempt(root, True, True)
            for trace_change in ({"failed":True},{"sessions":[{"generation":1,"viewport":True,"background":False,"auto_tile":True}]}):
                write();path=root / "completion-trace.json";value=json.loads(path.read_text());value.update(trace_change);path.write_text(json.dumps(value))
                with self.assertRaises(ValueError):
                    module.require_completed_attempt(root, True, True)

    def test_partial_or_idle_transfer_is_not_a_completed_proof(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            stages = ["cycles-before", "cycles-color-edited", "cycles-color-undone", "cycles-color-redone", "cycles-camera-only", "cycles-reentered-latest"]
            path = root / "cycles-udim.json"
            path.write_text(json.dumps({"captures": [{"stage": name} for name in stages]}))
            log = "BTS_BA_IDLE_BEGIN\nBTS_BA_IDLE_END\nBTS_BA_CAMERA_BEGIN\nBTS_BA_CAMERA_END\nBTS_BA_UDIM_DONE\n"
            (root / "run.log").write_text(log)
            module.require_completed_attempt(root, True)
            for marker in ("BTS_CYCLES_MIRROR_CAPTURE", "BTS_CYCLES_RELOAD", "BTS_CYCLES_IMAGE_RELOAD"):
                (root / "run.log").write_text(log.replace("BTS_BA_IDLE_END", marker + "\nBTS_BA_IDLE_END"))
                with self.assertRaises(ValueError):
                    module.require_completed_attempt(root, True)
            (root / "run.log").write_text(log.replace("BTS_BA_UDIM_DONE", ""))
            with self.assertRaises(ValueError):
                module.require_completed_attempt(root, True)
            (root / "run.log").write_text(log)
            path.write_text(json.dumps({"captures": [{"stage": stages[0]}]}))
            with self.assertRaises(ValueError):
                module.require_completed_attempt(root, True)

    def test_current_and_stale_inputs(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            sources = {}
            for relative in module.required_sources("udim"):
                path = root / relative
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text("compiled source " + relative)
                sources[relative] = module.sha256(path)
            source = root / "native/host/NativeCyclesImages.cc"
            patches = {}
            for name in ("0035-hosted-live-image-cache.patch", "0036-cycles-live-image-reload.patch"):
                path = root / "native/patches" / name
                path.parent.mkdir(exist_ok=True)
                path.write_text(name)
                patches[str(path.relative_to(root))] = module.sha256(path)
            binary = root / "test-host"
            binary.write_bytes(b"compiled binary")
            parent = root / "body-refresh.json"
            parent.write_text("actual compile manifest")
            path = root / "run-manifest.json"
            manifest = {"schema": 1, "mode": "udim", "sanitized_bridge_only": False,
                        "source_sha256": sources,
                        "patch_sha256": patches,
                        "parents": [{"path": str(parent), "sha256": module.sha256(parent)}],
                        "binaries": {str(binary.resolve()): module.sha256(binary)}}
            path.write_text(json.dumps(manifest))
            validate = lambda: module.validate_manifest(path, root, binary, "udim", False)
            validate()
            for target in (source, root / next(iter(patches)), parent, binary):
                original = target.read_bytes()
                target.write_bytes(original + b" changed")
                with self.assertRaises(ValueError):
                    validate()
                target.write_bytes(original)
                validate()
            with self.assertRaises(ValueError):
                module.validate_manifest(path, root, binary, "128", False)
            with self.assertRaises(ValueError):
                module.validate_manifest(path, root, binary, "udim", True)
            # Each reviewed persistence/worker extension must be recorded too;
            # a preceding build manifest cannot be relabelled as the new fork.
            for name in ("0037-cycles-hosted-displacement-image-refresh.patch",
                         "0038-hosted-immutable-source-snapshots.patch",
                         "0040-partial-write-owned-embedded-self-cache.patch",
                         "0041-hosted-shader-worker-contexts.patch",
                         "0042-hosted-stack-program-warmup.patch"):
                extension = root / "native/patches" / name
                extension.write_text("reviewed synthetic extension " + name)
                with self.assertRaises(ValueError):
                    validate()
                patches[str(extension.relative_to(root))] = module.sha256(extension)
                path.write_text(json.dumps(manifest))
                validate()
                original = extension.read_text()
                extension.write_text(original + " changed")
                with self.assertRaises(ValueError):
                    validate()
                extension.write_text(original)
            first, second = module.fresh_runtime(root), module.fresh_runtime(root)
            (first / "old-proof.json").write_text("old")
            self.assertNotEqual(first, second)
            self.assertFalse((second / "old-proof.json").exists())
            frozen = module.freeze_admission(path, binary, second)
            self.assertEqual(frozen.read_bytes(), binary.read_bytes())
            binary.write_bytes(b"a later link")
            self.assertEqual(frozen.read_bytes(), b"compiled binary")
            self.assertEqual((second / "provenance/run-manifest.json").read_bytes(), path.read_bytes())
            binary.write_bytes(b"compiled binary")
            extension = root / "native/patches/0037-cycles-hosted-displacement-image-refresh.patch"
            extension.write_text("reviewed37 fixture")
            # Merely adding the new source cannot make the old binary manifest
            # current. Both complete series and run-level identity must agree.
            with self.assertRaises(ValueError):
                validate()
            patches[str(extension.relative_to(root))] = module.sha256(extension)
            path.write_text(json.dumps(manifest))
            validate()
            extension.write_text("changed37")
            with self.assertRaises(ValueError):
                validate()
            patches[str(extension.relative_to(root))] = module.sha256(extension)
            path.write_text(json.dumps(manifest))
            validate()
            immutable = root / "native/patches/0038-hosted-immutable-source-snapshots.patch"
            immutable.write_text("reviewed38 fixture")
            with self.assertRaises(ValueError):
                validate()  # Old run manifest cannot claim the new fork boundary.
            patches[str(immutable.relative_to(root))] = module.sha256(immutable)
            path.write_text(json.dumps(manifest))
            validate()
            immutable.write_text("changed38")
            with self.assertRaises(ValueError):
                validate()


if __name__ == "__main__":
    unittest.main()
