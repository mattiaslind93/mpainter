"""Synthetic burst admission only; no Qt/Blender initialization or GPU IO."""
import copy
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("burst_classifier", ROOT / "scripts/host/check-cycles-live-burst.py")
BURST = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(BURST)


class BurstClassifierTests(unittest.TestCase):
    def fixture(self, root, publishes=1):
        owner = {"material_uid": 77, "set_id": "fixture-set", "color_image_uid": 88, "snapshot_generation": "0"}
        tiles = lambda digest: [{"channel": c, "tile": t, "bytes": 2048 * 2048 * 16, "sha256": digest * 64, "gpu_cpu_bit_exact": True} for c in (0, 2) for t in (1001, 1002)]
        stages = [{"stage": name, "owner": owner.copy(), "handles": ["fixed"], "graph": "7", "svm": "1", "full_tiles": tiles("a" if name in ("baseline", "undone") else "b")} for name in ("baseline", "released", "undone", "redone")]
        frames = [{"owner": {**owner, "snapshot_generation": str(i + 1)}, "render": {"image_mirror_paused": False, "status": "Preparing live material images"}, "color_upload_callbacks": str(i + 10), "cpu": [{"channel": c, "tile": t, "image_uid": 88 if c == 0 else 89, "pixel": [int(c == 0 and bool(publishes) and i > 5), 0, 0, 1]} for c in (0, 2) for t in (1001, 1002)]} for i in range(12)]
        report = {"profile": "held-picker-burst-6s-v1", "held_seconds": 6, "moves": 12, "during_hold_gpu_readbacks": 0, "during_hold_full_tile_hashes": 0,
                  "held_frames": frames, "captures": stages, "issued": [{"kind": "move", "color": [i / 12, .2, .3, 1]} for i in range(12)]}
        lines = ["BTS_BA_BURST_BEGIN"]
        lines += [f"BTS_GPU_CHANNEL_PUBLISH channel=0 generation={i + 1} queue_ms=0" for i in range(12)]
        lines += ["BTS_CYCLES_MIRROR_CAPTURE", "BTS_CYCLES_MIRROR_POLL tiles=2 ready=1", "BTS_CYCLES_MIRROR_CAPTURE", "BTS_CYCLES_MIRROR_POLL tiles=2 ready=1", "BTS_CYCLES_MIRROR_CAPTURE"]
        lines += ["BTS_CYCLES_MIRROR_PUBLISH"] * publishes
        lines += ["BTS_BA_BURST_END", "BTS_BA_BURST_DONE"]
        self.save(root, report, "\n".join(lines))
        return report, "\n".join(lines)

    def save(self, root, report, log):
        (root / "cycles-live-burst.json").write_text(json.dumps(report))
        (root / "run.log").write_text(log)

    def test_real_publish_generations_and_starvation_are_distinct(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.fixture(root, 1)
            self.assertFalse(BURST.check(root)["starvation_observed"])
            self.fixture(root, 0)
            result = BURST.check(root)
            self.assertTrue(result["starvation_observed"])
            self.assertEqual(result["consumed_cpu_cycles_lower_bound"], 2)
            self.assertEqual(result["held_fixture_gpu_write_generations"], 12)

    def test_foreign_set_or_generations_cannot_satisfy_writes(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            report, log = self.fixture(root, 0)
            foreign = copy.deepcopy(report)
            foreign["held_frames"][4]["owner"]["material_uid"] = 999
            self.save(root, foreign, log)
            with self.assertRaisesRegex(ValueError, "owner"):
                BURST.check(root)
            self.save(root, report, log.replace("generation=", "generation=99"))
            with self.assertRaisesRegex(ValueError, "GPU writes"):
                BURST.check(root)

    def test_incomplete_or_no_cpu_cycle_is_inconclusive(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            report, log = self.fixture(root, 0)
            self.save(root, report, log.replace("BTS_BA_BURST_DONE", ""))
            with self.assertRaisesRegex(ValueError, "Incomplete"):
                BURST.check(root)
            self.save(root, report, log.replace("ready=1", "ready=0"))
            with self.assertRaisesRegex(ValueError, "capture cycles"):
                BURST.check(root)
            broken = copy.deepcopy(report)
            broken["captures"][2]["full_tiles"].pop()
            self.save(root, broken, log)
            with self.assertRaisesRegex(ValueError, "full-tile"):
                BURST.check(root)

    def test_proxy_callbacks_or_constant_picker_are_insufficient(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            report, log = self.fixture(root, 0)
            self.save(root, report, log.replace("BTS_GPU_CHANNEL_PUBLISH", "UNRELATED_CALLBACK"))
            with self.assertRaisesRegex(ValueError, "GPU writes"):
                BURST.check(root)
            constant = copy.deepcopy(report)
            for event in constant["issued"]:
                event["color"] = [1, 1, 1, 1]
            self.save(root, constant, log)
            with self.assertRaisesRegex(ValueError, "picker"):
                BURST.check(root)

    def test_zero_markers_changed_or_invalid_cpu_samples_are_inconclusive(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            report, log = self.fixture(root, 0)
            changed = copy.deepcopy(report)
            changed["held_frames"][4]["cpu"][3]["pixel"][0] = .5
            self.save(root, changed, log)
            with self.assertRaisesRegex(ValueError, "contradict"):
                BURST.check(root)
            for mutation in ("missing", "empty", "short", "nonfinite", "foreign"):
                broken = copy.deepcopy(report)
                samples = broken["held_frames"][4]["cpu"]
                if mutation == "missing": del broken["held_frames"][4]["cpu"]
                elif mutation == "empty": samples.clear()
                elif mutation == "short": samples[0]["pixel"] = [1]
                elif mutation == "nonfinite": samples[0]["pixel"][0] = float('nan')
                else: samples[0]["image_uid"] = 999
                self.save(root, broken, log)
                with self.assertRaisesRegex(ValueError, "CPU"):
                    BURST.check(root)

    def test_error_or_wrong_cpu_oracle_never_classifies_starvation(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            report, log = self.fixture(root, 0)
            paused = copy.deepcopy(report)
            paused["held_frames"][0]["render"]["image_mirror_paused"] = True
            self.save(root, paused, log)
            with self.assertRaisesRegex(ValueError, "mirror error"):
                BURST.check(root)
            broken = copy.deepcopy(report)
            broken["captures"][1]["full_tiles"][0]["gpu_cpu_bit_exact"] = False
            self.save(root, broken, log)
            with self.assertRaisesRegex(ValueError, "full-tile"):
                BURST.check(root)


if __name__ == "__main__":
    unittest.main()
