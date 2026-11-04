"""Actual BM menus and Smudge/Clone input on an owned two-tile fixture.

No CPU image sampling/composition and no product mutation hook. Explicit GPU
probe/capture readbacks occur after the interactive measurement, never inside it.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import signal
import time

from private_gpu_gui import private_gpu_gui

parser = argparse.ArgumentParser()
parser.add_argument("--output", type=Path, required=True)
parser.add_argument("--expected-binary-sha256", required=True)
parser.add_argument("--existing", action="store_true")
args = parser.parse_args()
source = Path(__file__).resolve().parents[2]
bts = Path(os.environ.get("BTS_ROOT", source.parent)).resolve()
output = args.output.resolve()
assert output.is_relative_to((bts / "build").resolve()), "Owned build output required"
binary = Path(os.environ["BTS_HOST_EXECUTABLE"]).resolve()


def sha(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


assert sha(binary) == args.expected_binary_sha256
owned = ["native/host/GpuAdjustmentPaintDemo.hh", "native/host/NativeAdjustmentState.inc", "native/host/MaterialInspection.cc", "native/host/NativeCyclesImages.cc", "scripts/host/verify-adjustment-paint-ui.py"]
source_hashes = {name: sha(source / name) for name in owned}
if not args.existing:
    assert not output.exists(), "Keep every previous run intact"
    output.mkdir(parents=True)
    fixtures = output / "fixtures"
    fixtures.mkdir()
    (fixtures / "bm-ui.mtl").write_text("newmtl BMWhite\nKd 1 1 1\n")
    lines = ["mtllib bm-ui.mtl", "o BM_UI_Two_Tiles", "usemtl BMWhite"]
    for tile in range(2):
        x = tile * 1.1
        lines += [f"v {x} 0 0", f"v {x+1} 0 0", f"v {x+1} 1 0", f"v {x} 1 0"]
        lines += [f"vt {tile+.05} .05", f"vt {tile+.95} .05", f"vt {tile+.95} .95", f"vt {tile+.05} .95"]
        indices = range(tile * 4 + 1, tile * 4 + 5)
        lines += ["f " + " ".join(f"{i}/{i}" for i in indices)]
    mesh = fixtures / "bm-ui.obj"
    mesh.write_text("\n".join(lines) + "\n")
    environment = Path(os.environ.get("BTS_ENVIRONMENT", bts / "build/shared-fixtures/TT_environmentLights_v001.hdr"))
    assert environment.is_file()
    env = dict(os.environ, BTS_ROOT=str(bts), BTS_HOST_EXECUTABLE=str(binary), BTS_ENVIRONMENT=str(environment), BTS_BM_ADJUSTMENT_DEMO="1", BTS_PERFORMANCE_PROFILE="1", BTS_SESSION_LOG_DIR=str(output / "logs"), XDG_STATE_HOME=str(output / "state"))
    (output / "state").mkdir()
    for key in ("BTS_VERIFY_GPU_STACK", "BTS_BJ_PROJECTION_UI", "BTS_BN_SMART_MASK", "BTS_BN_SPMSK_UI", "BTS_BK_FOLDER_MASK", "BTS_STROKE_TARGET_PROFILE", "BTS_BO_BRUSH_PRESETS"):
        env.pop(key, None)
    command = ["bash", str(source / "scripts/host/run-host.sh"), "--no-splash", "--workspace", "--gui-fix-demo", str(output), "--gui-fix-mesh", str(mesh), "--gui-fix-resolution", "2048", "--autosave-seconds", "3600", "--library-dir", str(output / "library")]
    receipt = {"complete": False, "binary_sha256": sha(binary), "source_hashes": source_hashes, "fixture_sha256": sha(mesh), "environment_sha256": sha(environment), "command": command, "actual_exit": None}
    (output / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n")
    start = time.monotonic()
    with private_gpu_gui(output, env, source) as (prefix, actual_env), (output / "run.log").open("w") as log:
        process = subprocess.Popen(prefix + command, cwd=source, env=actual_env, stdout=log, stderr=subprocess.STDOUT, start_new_session=True)
        receipt["pid"] = process.pid
        (output / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n")
        try:
            code = process.wait(timeout=360)
        except subprocess.TimeoutExpired:
            os.killpg(process.pid, signal.SIGTERM)
            try:
                code = process.wait(timeout=15)
            except subprocess.TimeoutExpired:
                os.killpg(process.pid, signal.SIGKILL)
                code = process.wait()
            receipt["timeout_seconds"] = 360
    receipt.update(actual_exit=code, seconds=time.monotonic() - start)
    (output / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n")

receipt = json.loads((output / "receipt.json").read_text())
log = (output / "run.log").read_text()
assert receipt["actual_exit"] == 0, receipt
assert sha(binary) == args.expected_binary_sha256 == receipt["binary_sha256"]
assert all(sha(source / name) == digest for name, digest in receipt["source_hashes"].items())
assert "BTS_SHUTDOWN clean" in log and "BTS_BM_ADJUSTMENT_UI_PASS" in log
assert not any(value in log for value in ("BTS_HOST_FAILED", "BTS_GL_ERROR", "BTS_BRUSH_CPU_FALLBACK", "BTS_GPU_AUTHORING_CPU_READ", "Not freed memory blocks"))
proof = json.loads((output / "adjustment-paint-ui.json").read_text())
pickers = proof["project_pickers"]
assert [row["operation"] for row in pickers] == ["Save As", "Open", "Open"]
expected_project = str(output / "bm-adjustment-ui.blend")
for row in pickers:
    assert row["filename"] == row["expected_filename"] == expected_project
    assert row["selected_files"] == [expected_project]
    assert row["parent_exists"] and Path(row["parent_directory"]) == output
    assert row["current_directory"] and row["button_text"]
    assert row["button_enabled"] and row["button_visible"] and row["button_focus"]
    assert row["button_geometry"][2] > 0 and row["button_geometry"][3] > 0
    assert row["button_down_before"] is False and row["button_down_after_press"] is True
    assert row["button_down_after_release"] is False
    assert row["input"] == "focused Space press/release" and row["closed"]
    assert all(row[name] == 1 for name in ["pressed", "released", "clicked", "accepted"])
    assert (row["file_mode"], row["accept_mode"]) == ((0, 1) if row["operation"] == "Save As" else (1, 0))
    assert row["expected_file_exists"] == (row["operation"] == "Open")
assert [row["tool"] for row in proof["strokes"]] == ["lower-paint", "smudge", "clone", "recovery-smudge"]
summary = []
for stroke in proof["strokes"]:
    before, after = stroke["before"], stroke["after"]
    assert before["bm_tiles"] == after["bm_tiles"] == [1001, 1002]
    assert len(before["bm_dimensions"]) == len(after["bm_dimensions"]) == 2
    assert all(row["width"] == row["height"] == 2048 for d in [before,after] for row in d["bm_dimensions"])
    assert after["undo_count"] == before["undo_count"] + 1
    assert after["cpu_materializations"] == before["cpu_materializations"]
    assert after["cpu_layer_compositions"] == before["cpu_layer_compositions"]
    assert before["bm_probes"] != after["bm_probes"]
    if stroke["tool"] != "lower-paint":
        assert before["bm_lower"] == after["bm_lower"]
        assert before["bm_layer"]["coverage"] == ("painted" if stroke["tool"] == "recovery-smudge" else "empty")
        assert after["bm_layer"]["coverage"] == "painted"
        assert before["bm_selected_mask"] is False and after["bm_selected_mask"] is False
    transfers = stroke["pointer_transfers"]
    assert transfers["finish"]["calls"] == 0
    # Existing stroke-target accounting: fence-ready 48x48 thumbnail SSBOs and
    # 32-byte completion packets are distinct from synchronous authoring reads.
    read = transfers["readback"]
    calls, byte_count = int(read["calls"]), int(read["bytes"])
    packets, remainder = divmod(byte_count - 32 * calls, 48 * 48 * 16 - 32)
    assert remainder == 0 and 0 <= packets <= calls, (stroke["tool"], read)
    summary.append({"tool": stroke["tool"], "one_Undo": True, "interactive_CPU_delta": 0, "GPU_finish_calls": 0, "async_thumbnail_packets": packets, "async_status_packets": calls - packets})
stages = {capture["stage"]: capture for capture in proof["captures"]}
for name in ("adjustment-empty", "smudge-painted", "smudge-undo", "smudge-redo", "clone-painted", "clone-undo", "clone-redo", "adjustment-final", "adjustment-saved", "adjustment-reopened", "adjustment-refused", "adjustment-recovered"):
    assert name in stages and (output / (name + ".png")).is_file()
for tool in ("smudge", "clone"):
    row = next(s for s in proof["strokes"] if s["tool"] == tool)
    assert stages[tool + "-undo"]["bm_probes"] == row["before"]["bm_probes"]
    assert stages[tool + "-redo"]["bm_probes"] == row["after"]["bm_probes"]
assert stages["adjustment-saved"]["bm_probes"] == stages["adjustment-reopened"]["bm_probes"]
assert (output / "bm-adjustment-ui.blend").is_file() and (output / "bm-adjustment-witness.json").is_file()
for stage in ["smudge-adjustment-saved", "smudge-adjustment-reopened"]:
    assert stage in stages and (output / (stage + ".png")).is_file()
recovery_after = next(s for s in proof["strokes"] if s["tool"] == "recovery-smudge")["after"]
assert stages["smudge-adjustment-saved"]["bm_probes"] == stages["smudge-adjustment-reopened"]["bm_probes"] == recovery_after["bm_probes"]
assert stages["smudge-adjustment-reopened"]["bm_probes"] != stages["adjustment-reopened"]["bm_probes"]
final_witness = json.loads((output / "bm-adjustment-witness.json").read_text())
assert final_witness["project_pickers"] == pickers
assert stages["smudge-adjustment-reopened"]["project_pickers"] == pickers
assert final_witness["tools"] == ["clone", "smudge"]
assert final_witness["gpu"] == recovery_after["bm_probes"]
assert final_witness["packed_file_sha256"] == sha(output / "bm-adjustment-ui.blend")
refusal = proof["refusal"]
before, after = refusal["before"], refusal["after"]
reason = after["bm_refusal"]
assert reason["fired"] and reason["terminal"] and reason["stroke_cancelled"]
assert reason["needed_bytes"] == 131072 and reason["available_bytes"] == 0 and "MiB" in reason["reason"]
assert all(before[key] == after[key] for key in ("undo_count", "bm_layer", "bm_lower", "bm_probes", "cpu_materializations", "cpu_layer_compositions"))
assert refusal["pointer_transfers"]["finish"]["calls"] == 0
checks = {row["stage"]: row for row in proof["checks"]}
assert set(checks) == {"normal-export", "flatten-installation", "flatten-undone", "cycles-adjustment-painted", "cycles-adjustment-undo", "cycles-adjustment-redo", "smudge-packed-persistence"}
cycles_revisions = {}
for stage in ["cycles-adjustment-painted", "cycles-adjustment-undo", "cycles-adjustment-redo"]:
    row = checks[stage]
    assert row["captured_frame"] > row["done_frame"] >= 0 and "Sample " in row["observed_sample_status"]
    assert "Rendering Done" in row["render"]["status"]
    assert len(row["packets"]) == 4
    assert {(p["tile"], p["role"]) for p in row["packets"]} == {(t, c) for t in [1001, 1002] for c in [0, 2]}
    assert (output / (stage + ".png")).is_file()
    for packet in row["packets"]:
        key = packet["tile"], packet["role"]
        assert packet["current"] and packet["wanted"] == packet["published"] == packet["packet_revision"]
        assert int(packet["published"]) > cycles_revisions.get(key, 0)
        cycles_revisions[key] = int(packet["published"])
        assert packet["width"] == packet["height"] == 2048 and len(packet["samples"]) == 3
    if stage != "cycles-adjustment-painted":
        delta = row["previous_frame_delta"]
        assert delta["changed_pixels"] > 64 and delta["mean_absolute_rgb"] > .002
        emission = row["signed_emission_change"]
        predicted, observed = emission["predicted_linear_luminance_change"], emission["observed_display_luminance_change"]
        assert abs(predicted) > .02 and abs(observed) > .01 and predicted * observed > 0
        assert emission["inspection"] == 0 and emission["patch_size"] == 5
        assert row["canonical_state"]["bm_inspection"] == 0
        assert emission["tile"] in [1001,1002] and len(emission["frame_xy"]) == 2
recovery = next(s for s in proof["strokes"] if s["tool"] == "recovery-smudge")
for stage, expected in [("cycles-adjustment-painted", recovery["after"]), ("cycles-adjustment-undo", recovery["before"]), ("cycles-adjustment-redo", recovery["after"])]:
    actual = checks[stage]["canonical_state"]
    assert all(actual[k] == expected[k] for k in ["bm_layer", "bm_lower", "bm_probes", "cpu_materializations", "cpu_layer_compositions"])
    for packet in checks[stage]["packets"]:
        source_packet = next(p for p in expected["bm_probes"] if p["tile"] == packet["tile"] and p["channel"] == packet["role"])
        for point in packet["samples"]:
            want = next(p["rgba"] for p in source_packet["points"] if p["xy"] == point["xy"])
            assert all(abs(a-b) < 3e-6 for a,b in zip(point["rgba"], want))
persistence = checks["smudge-packed-persistence"]
assert persistence["project_pickers"] == pickers
assert persistence["same_as_cycles_redo"] and persistence["different_from_clone"]
assert persistence["clone_packed_sha256"] != persistence["final_packed_sha256"] == final_witness["packed_file_sha256"]
assert persistence["gpu"] == final_witness["gpu"]
export = checks["normal-export"]
assert export["gpu_capture"]["tiles"] == 2 and export["gpu_capture"]["packets"] > 0
assert all(Path(name).is_file() and Path(name).stat().st_size > 0 for name in export["files"])
assert all(any(str(tile) in Path(name).name for name in export["files"]) for tile in [1001,1002])
assert checks["flatten-installation"]["maximum_gpu_error"] < 3e-6 and checks["flatten-undone"]["exact_source_GPU_history"]
for name in ["normal-export.png", "adjustment-flattened.png", "adjustment-flatten-undone.png"]:
    assert (output / name).is_file()
receipt.update(complete=True, clean_shutdown=True, project_pickers=pickers, strokes=summary, screenshot_sha256=sha(output / "adjustment-final.png"), proof_sha256=sha(output / "adjustment-paint-ui.json"), scope="Actual menu/context/Smudge/Clone pointer UI, same-process GUI Save/Open, ordinary GPU Export/Flatten, and successful two-UDIM Color/Roughness publication -> Cycles Sample/Done/later frames across Undo/Redo. Explicit probes/captures after interaction. Backend independent numerical, data-first fresh cold and performance gates are separate.")
(output / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n")
print(json.dumps(receipt, indent=2))
