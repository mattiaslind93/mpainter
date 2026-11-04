"""AB signed UDIM/alpha import, actual GPU image, undo and packed-project regression."""
import argparse
import json
import os
import re
import subprocess
from pathlib import Path

p = argparse.ArgumentParser()
p.add_argument("--output", type=Path, required=True)
p.add_argument("--large", action="store_true")
p.add_argument("--gui-only", action="store_true")
p.add_argument("--cpu-only", action="store_true")
p.add_argument("--source-copy", type=Path)
a = p.parse_args()
root = Path(__file__).resolve().parents[2]
out = a.output.resolve()
assert out.is_relative_to(root.parent / "build")
out.mkdir(parents=True, exist_ok=True)
env = dict(BTS_ROOT=str(root.parent), QT_QPA_PLATFORM="offscreen",
           BTS_HEADLESS_WORKSPACE="1", LANG="sv_SE.UTF-8", LC_ALL="sv_SE.UTF-8",
           BTS_TEXTURE_IMPORT_FIXTURES=str(out / "fixtures"))
for key, folder in [("XDG_CONFIG_HOME", "config"), ("XDG_DATA_HOME", "data"),
                    ("XDG_CACHE_HOME", "cache"), ("BTS_RECOVERY_DIR", "recovery"),
                    ("BTS_RESOURCE_CACHE", "resources"), ("TMPDIR", "tmp"),
                    ("BTS_SESSION_LOG_DIR", "logs")]:
    (out / folder).mkdir(exist_ok=True)
    env[key] = str(out / folder)
if a.large:
    env["BTS_TEXTURE_IMPORT_LARGE"] = "1"
if a.source_copy:
    source = a.source_copy.resolve()
    assert source.is_relative_to(root.parent / "build") and source.is_file()
    env["BTS_AB_SOURCE_COPY"] = str(source)


def run(label, args, extra=None):
    command = ["distrobox", "enter", "bts-build", "--", "env"]
    command += [f"{k}={v}" for k, v in (env | (extra or {})).items()]
    command += ["nice", "-n", "19", "bash", str(root / "scripts/host/run-host.sh")] + args
    with (out / f"{label}.log").open("w") as log:
        result = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, timeout=600)
    assert result.returncode == 0, (label, result.returncode,
                                  (out / f"{label}.log").read_text()[-5000:])


if not a.gui_only:
    run("cpu", ["--cpu-texture-import-test", str(out / "fixtures")])
cpu = json.loads((out / "fixtures/texture-import-cpu.json").read_text())
assert len(cpu["ab_displacement"]) == 2
for case in cpu["ab_displacement"]:
    assert case["raw_zero_alpha_coverage_one"] and case["export_exact"] and case["shared_stats"]
    assert case["minimum"] < 0 and case["maximum"] > 1 and case["midlevel"] == 0
if a.cpu_only:
    result = {"cpu": cpu["ab_displacement"], "actual_source_copy": cpu["ab_actual_source_copy"], "large": a.large}
    (out / "verified.json").write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps(result, indent=2))
    raise SystemExit(0)
run("workspace", ["--workspace", "--gui-fix-demo", str(out / "workspace"),
                  "--gui-fix-mesh", str(root / "fixtures/two_tile_udim.obj"),
                  "--gui-fix-resolution", "4096" if a.large else "512"],
    {"BTS_AB_DISPLACEMENT": "1"})
proof = json.loads((out / "workspace/ab-displacement.json").read_text())
captures = {x["stage"]: x for x in proof["captures"]}
for name, capture in captures.items():
    assert not capture["shader_failures"], (name, capture["shader_failures"])
    assert capture["input_space"] == "Non-Color/raw"
    masked = name in ("explicit-alpha", "redo-alpha")
    assert capture["alpha_control"] == masked
    assert "R min" in capture["resource_info"] and "Midlevel" in capture["resource_info"]
    assert abs(capture["midlevel"] - (.23 if name in ("custom-midlevel", "packed-open") else 0)) < 1e-6
    assert capture["strength"] == 1
    assert {x["tile"] for x in capture["ab_samples"]} == {1001, 1002}
    for sample in capture["ab_samples"]:
        assert sample["gpu_resident"], (name, sample)
        expected = -.001 if sample["tile"] == 1001 else .002
        assert sample["cpu"][1] == (0 if masked else 1), (name, sample)
        assert abs(sample["native"][3] - (0 if masked else 1)) < 1e-6, (name, sample)
        assert abs(sample["native"][0] - (0 if masked else expected)) < 1e-6, (name, sample)
        if not masked:
            assert abs(sample["cpu"][0] - expected) < 1e-6
assert captures["custom-midlevel"]["document"] == captures["packed-open"]["document"]
log = (out / "workspace.log").read_text()
assert "BTS_SHUTDOWN clean" in log
assert not re.search(r"Shader compilation failed|BTS_DRAW_FAILED|BTS_HOST_FAILED", log)
result = {"cpu": cpu["ab_displacement"], "actual_source_copy": cpu["ab_actual_source_copy"], "native_pid": proof["pid"],
          "gui_stages": list(captures), "alpha_ui_undo_redo": True,
          "native_float32_zero_alpha_signed_samples": True,
          "packed_open_preserves_custom_midlevel": True, "locale": "sv_SE.UTF-8"}
(out / "verified.json").write_text(json.dumps(result, indent=2) + "\n")
print(json.dumps(result, indent=2))
