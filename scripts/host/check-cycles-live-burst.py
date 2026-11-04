#!/usr/bin/env python3
"""Classify one retained burst attempt; never compiles, runs, or retries it."""
import argparse
import json
from pathlib import Path
import re
import math


def check(runtime):
    runtime = Path(runtime)
    report = json.loads((runtime / "cycles-live-burst.json").read_text())
    log = (runtime / "run.log").read_text()
    if report.get("profile") != "held-picker-burst-6s-v1" or report.get("held_seconds") != 6 or report.get("during_hold_gpu_readbacks") != 0 or report.get("during_hold_full_tile_hashes") != 0:
        raise ValueError("Wrong burst profile or intrusive held-window work")
    if log.count("BTS_BA_BURST_BEGIN") != 1 or log.count("BTS_BA_BURST_END") != 1 or log.count("BTS_BA_BURST_DONE") != 1 or "BTS_BA_BURST_FAILED" in log:
        raise ValueError("Incomplete or failed burst attempt")
    held = log.split("BTS_BA_BURST_BEGIN", 1)[1].split("BTS_BA_BURST_END", 1)[0]
    captures = len(re.findall(r"BTS_CYCLES_MIRROR_CAPTURE\b", held))
    copies = len(re.findall(r"BTS_CYCLES_MIRROR_POLL\b[^\n]*\bready=1\b", held))
    publications = len(re.findall(r"BTS_CYCLES_MIRROR_PUBLISH\b", held))
    if any(marker in held for marker in ("BTS_GPU_STACK_ERROR", "BTS_DRAW_FAILED", "BTS_BA_BURST_FAILED")):
        raise ValueError("Producer/renderer error: not revision starvation")
    # A new capture cannot start while the previous CPU future is still owned:
    # the production state machine must consume it before reaching submit.
    consumed = min(copies, max(0, captures - 1))
    frames = report["held_frames"]
    for frame in frames:
        render = frame.get("render", {})
        status = render.get("status", "").lower()
        if render.get("image_mirror_paused") is not False or "error" in status or "failed" in status:
            raise ValueError("Renderer/mirror error or missing status: not revision starvation")
    if report.get("moves", 0) < 12 or len(frames) < 12 or consumed < 2:
        raise ValueError("Inconclusive burst: insufficient held frames/capture cycles")
    callbacks = int(frames[-1]["color_upload_callbacks"]) - int(frames[0]["color_upload_callbacks"])
    colors = [tuple(event.get("color", [])) for event in report.get("issued", []) if event.get("kind") == "move"]
    if any(len(color) != 4 or any(not isinstance(value, (float, int)) or not math.isfinite(value) for value in color) for color in colors) or len(set(colors)) < 12:
        raise ValueError("Inconclusive burst: insufficient distinct actual picker colors")
    owner_fields = ("material_uid", "set_id", "color_image_uid")
    owner = report["captures"][0]["owner"]
    if any(not owner.get(field) for field in owner_fields):
        raise ValueError("Missing fixture material/set/image identity")
    generations = set()
    for frame in frames:
        actual = frame.get("owner", {})
        if any(actual.get(field) != owner[field] for field in owner_fields):
            raise ValueError("Burst owner material/set/image changed")
        generations.add(str(actual.get("snapshot_generation", "")))
    # The actor admits exactly one live binding on every held frame. Thus the
    # post-wrote_image channel0 marker and matching binding generation belong
    # to this fixture; an unrelated material cannot satisfy this threshold.
    written = set(re.findall(r"BTS_GPU_CHANNEL_PUBLISH channel=0 generation=(\d+)\b", held)) & generations
    if callbacks < 8 or len(written) < 8:
        raise ValueError("Inconclusive burst: insufficient fixture GPU writes/generations")
    stages = report["captures"]
    if [stage["stage"] for stage in stages] != ["baseline", "released", "undone", "redone"]:
        raise ValueError("Missing exact post-release/Undo/Redo stages")
    baseline = stages[0]
    for stage in stages:
        if stage["handles"] != baseline["handles"] or stage["graph"] != baseline["graph"] or stage["svm"] != baseline["svm"]:
            raise ValueError("Authoritative allocation or shader compilation changed")
        tiles = stage["full_tiles"]
        if len(tiles) != 4 or {(v["channel"], v["tile"]) for v in tiles} != {(c, t) for c in (0, 2) for t in (1001, 1002)}:
            raise ValueError("Incomplete GPU/CPU full-tile equality evidence")
        for tile in tiles:
            if tile.get("gpu_cpu_bit_exact") is not True or int(tile.get("bytes", 0)) != 2048 * 2048 * 16 or not re.fullmatch(r"[0-9a-f]{64}", tile.get("sha256", "")):
                raise ValueError("Invalid exact full-tile witness")
    if stages[0]["full_tiles"] != stages[2]["full_tiles"] or stages[1]["full_tiles"] != stages[3]["full_tiles"]:
        raise ValueError("Undo/Redo failed exact tile restoration")
    signatures = []
    for frame in frames:
        samples = frame.get("cpu", [])
        if len(samples) != 4 or {(sample.get("channel"), sample.get("tile")) for sample in samples} != {(c, t) for c in (0, 2) for t in (1001, 1002)}:
            raise ValueError("Missing held CPU tile samples: inconclusive")
        for sample in samples:
            pixel = sample.get("pixel", [])
            if (len(pixel) != 4 or any(not isinstance(value, (float, int)) or not math.isfinite(value) for value in pixel) or
                    (sample["channel"] == 0 and sample.get("image_uid") != owner["color_image_uid"])):
                raise ValueError("Malformed or foreign held CPU sample: inconclusive")
        signatures.append(tuple((sample["channel"], sample["tile"], *sample["pixel"])
                                for sample in sorted(samples, key=lambda item: (item["channel"], item["tile"]))))
    if publications == 0 and len(set(signatures)) != 1:
        raise ValueError("Changing CPU samples contradict zero publication markers: inconclusive")
    color_samples = [tuple(sample["pixel"]) for frame in frames for sample in frame["cpu"] if sample["channel"] == 0 and sample["tile"] == 1001]
    result = {"held_upload_callbacks_proxy": callbacks, "held_fixture_gpu_write_generations": len(written), "distinct_picker_colors": len(set(colors)), "fixture_owner": {key: owner[key] for key in owner_fields}, "captures": captures, "completed_gpu_copies": copies,
              "consumed_cpu_cycles_lower_bound": consumed, "publications": publications,
              "distinct_cpu_color_samples": len(set(color_samples)),
              "starvation_observed": publications == 0, "performance_claim": False,
              "cycles_render_completion_claim": False}
    if publications and len(set(color_samples)) < 2:
        raise ValueError("Publication log and actual held CPU samples disagree")
    return result


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("runtime", type=Path)
    args = parser.parse_args()
    outcome = check(args.runtime)
    (args.runtime / "burst-assessment.json").write_text(json.dumps(outcome, indent=2) + "\n")
    print(json.dumps(outcome, indent=2))
    raise SystemExit(1 if outcome["starvation_observed"] else 0)
