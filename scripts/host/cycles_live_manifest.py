"""Immutable source/binary admission for the private Cycles regression runner."""
import hashlib
import json
import shutil
import subprocess
from pathlib import Path


def sha256(path):
    digest = hashlib.sha256()
    with Path(path).open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def required_sources(mode):
    core = {"native/host/" + name for name in (
        "main.cc", "BlenderHost.cc", "MaterialInspection.cc", "MaterialInspection.hh",
        "NativeCyclesImages.cc", "NativeCyclesImages.hh", "NativeCyclesImageCache.hh",
        "NativeCyclesPublication.hh")}
    if mode == "displacement":
        drivers = ("cycles_live_displacement_main.cc", "cycles_live_displacement_artist_test.cc",
                   "CyclesLiveDisplacementArtist.hh", "CyclesLiveDisplacementProbe.hh", "CyclesLiveDisplacementProbe.cc")
        return core | {"native/tests/" + name for name in drivers}
    drivers = ("cycles_live_udim_artist_main.cc", "cycles_live_udim_artist_test.cc",
               "CyclesLiveUdimArtist.hh") if mode == "udim" else ("cycles_live_artist_test.cc",)
    return core | {"native/tests/" + name for name in drivers}


def validate_manifest(path, repo, binary, mode, sanitized, profile="default"):
    """Reject stale inputs rather than relabel an earlier binary as current."""
    path, repo, binary = Path(path), Path(repo), Path(binary)
    manifest = json.loads(path.read_text())
    if manifest.get("schema") != 1 or manifest.get("mode") != mode:
        raise ValueError("Cycles run manifest does not match the requested driver")
    if manifest.get("sanitized_bridge_only") is not sanitized:
        raise ValueError("Cycles run manifest does not match the instrumentation")
    if manifest.get("profile", "default") != profile:
        raise ValueError("Cycles run manifest does not match the explicit diagnostic profile")
    sources = manifest.get("source_sha256", {})
    if profile == "held-picker-burst-6s-v1":
        from cycles_burst_snapshot import TEST_SOURCES
        required = TEST_SOURCES | {"native/tests/cycles_live_burst_plan.md"} | {"scripts/host/" + name for name in (
            "cycles_integrated_snapshot.py", "cycles_live_manifest.py", "verify-cycles-live.py",
            "capture-cycles-snapshot.py", "cycles_burst_snapshot.py", "check-cycles-live-burst.py")}
        if not required.issubset(sources):
            raise ValueError("Burst manifest lacks fixed source/runner admission")
    if profile == "live-displacement-controlled-v1":
        required = required_sources("displacement") | {
            "native/tests/cycles_live_displacement_plan.md", "native/tests/cycles_displacement_profile_test.py"
        } | {"scripts/host/" + name for name in (
            "cycles_integrated_snapshot.py", "cycles_live_manifest.py", "verify-cycles-live.py", "capture-cycles-snapshot.py")}
        if mode != "displacement" or sanitized or not manifest.get("integrated_snapshot") or not required.issubset(sources):
            raise ValueError("Displacement requires its full integrated source/runner admission")
    elif mode == "displacement":
        raise ValueError("Displacement requires explicit named profile admission")
    patches = manifest.get("patch_sha256", {})
    # The full integration validator rejects unreviewed series boundaries.
    # A37/38 checkout must record the extension too; never relabel an older binary.
    from cycles_integrated_snapshot import patch_series
    expected_patches = {name for name in patch_series(repo) if Path(name).name.startswith(("0035-", "0036-", "0037-", "0038-", "0040-", "0041-", "0042-"))}
    if not required_sources(mode).issubset(sources) or set(patches) != expected_patches:
        raise ValueError("Cycles run manifest lacks its source/fork identity")
    for relative, expected in {**sources, **patches}.items():
        target = (repo / relative).resolve()
        if not target.is_relative_to(repo.resolve()) or sha256(target) != expected:
            raise ValueError("Stale Cycles source or patch: " + relative)
    for parent in manifest.get("parents", []):
        if sha256(parent["path"]) != parent["sha256"]:
            raise ValueError("Stale Cycles compile/refresh manifest: " + parent["path"])
    expected = manifest.get("binaries", {}).get(str(binary.resolve()))
    if not expected or sha256(binary) != expected:
        raise ValueError("Stale or unrecorded Cycles test binary: " + str(binary))
    return manifest


def fresh_runtime(output):
    # Reusing configuration, reports or screenshots can make a failed attempt
    # appear successful. Each attempt owns a new directory and its own log.
    root = Path(output) / "runs"
    root.mkdir(exist_ok=True)
    import tempfile
    return Path(tempfile.mkdtemp(prefix="attempt-", dir=root))


def freeze_admission(manifest_path, binary, runtime):
    """Retain the admitted binary and provenance before another refresh/link."""
    manifest_path, binary, runtime = map(Path, (manifest_path, binary, runtime))
    manifest = json.loads(manifest_path.read_text())
    provenance = runtime / "provenance"
    provenance.mkdir()
    shutil.copyfile(manifest_path, provenance / "run-manifest.json")
    for index, parent in enumerate(manifest.get("parents", [])):
        shutil.copyfile(parent["path"], provenance / (str(index) + "-" + Path(parent["path"]).name))
    private_bin = runtime / "bin"
    private_bin.mkdir()
    retained = private_bin / binary.name
    # Independent reflinks are safe when a later linker truncates its output.
    # Never hardlink an executable or mutable compiler input into a proof.
    subprocess.run(["cp", "--reflink=auto", "--", str(binary), str(retained)], check=True)
    if sha256(retained) != manifest["binaries"][str(binary.resolve())]:
        raise ValueError("Cycles binary changed during admission")
    for resource in binary.parent.iterdir():
        if resource.is_dir() and resource.name[:1].isdigit():
            (private_bin / resource.name).symlink_to(resource.resolve())
    return retained


def require_completed_attempt(runtime, udim, controlled=False):
    runtime = Path(runtime)
    log = (runtime / "run.log").read_text()
    report = json.loads((runtime / ("cycles-udim.json" if udim else "cycles-live.json")).read_text())
    expected = ["cycles-before", "cycles-color-edited", "cycles-color-undone", "cycles-color-redone", "cycles-camera-only", "cycles-reentered-latest"] if udim else ["painting-before", "cycles-before", "cycles-edited", "cycles-undone", "cycles-redone", "painting-return"]
    if controlled:
        if not udim or report.get("profile") != "controlled-fixed-sampling-v1":
            raise ValueError("Missing explicit controlled diagnostic profile")
        expected.insert(1, "cycles-noop-control")
    elif report.get("profile", "default") != "default":
        raise ValueError("Controlled diagnostic requires explicit admission")
    if [capture.get("stage") for capture in report.get("captures", [])] != expected:
        raise ValueError("Cycles attempt did not complete all required artist stages")
    if ("BTS_BA_UDIM_DONE" if udim else "BTS_CYCLES_LIVE_ARTIST_DONE") not in log:
        raise ValueError("Cycles process exited before the final completion gate")
    if udim:
        for phase in ("IDLE", "CAMERA"):
            begin, end = "BTS_BA_" + phase + "_BEGIN", "BTS_BA_" + phase + "_END"
            if log.count(begin) != 1 or log.count(end) != 1:
                raise ValueError("Missing Cycles " + phase + " interval")
            interval = log.split(begin, 1)[1].split(end, 1)[0]
            if any(marker in interval for marker in ("BTS_CYCLES_MIRROR_", "BTS_CYCLES_RELOAD", "BTS_CYCLES_IMAGE_RELOAD")):
                raise ValueError("Cycles performed image transfer/reload during " + phase)
    if controlled:
        require_controlled_completion(runtime, report, log)
    return report


def require_controlled_completion(runtime, report, log):
    import re
    begin, end = "BTS_BA_NOOP_RELOAD_BEGIN", "BTS_BA_NOOP_RELOAD_END"
    if log.count(begin) != 1 or log.count(end) != 1:
        raise ValueError("Missing explicit no-op reload control interval")
    interval = log.split(begin, 1)[1].split(end, 1)[0]
    identifiers = re.findall(r"BTS_CYCLES_IMAGE_RELOAD texture_id=(\d+)", interval)
    if "BTS_CYCLES_MIRROR_" in interval or len(identifiers) != 2 or len(set(identifiers)) != 2 or len(re.findall(r"BTS_CYCLES_RELOAD images=2\b", interval)) != 1 or not set(re.findall(r"BTS_CYCLES_IMAGE_RELOAD texture_id=(\d+)", log)).issubset(identifiers):
        raise ValueError("No-op control did not perform only the two selective image reloads")
    trace = json.loads((runtime / "completion-trace.json").read_text())
    if trace.get("failed") or trace.get("failure"):
        raise ValueError("Completion observer recorded a failure")
    events = trace.get("events", [])
    sessions = trace.get("sessions", [])
    if not sessions or any(session.get("viewport") is not True or session.get("background") is not False or session.get("auto_tile") is not False for session in sessions) or len({session.get("generation") for session in sessions}) != len(sessions):
        raise ValueError("Completion observer lacks single-tile viewport Session proof")
    session_generations = {session["generation"] for session in sessions}
    previous = {}
    for event in events:
        generation = event["generation"]
        if generation in previous:
            old = previous[generation]
            if event["epoch"] != old["epoch"] + int(event["sample"] < old["sample"]):
                raise ValueError("Completion epoch is not tied to actual sample reset")
        elif event["epoch"] != 1:
            raise ValueError("Completion generation did not begin at observed epoch one")
        previous[generation] = event
    hashes, completions = {}, []
    for capture in report["captures"]:
        settings = capture.get("controlled_settings", {})
        if any(settings.get(name) is not False for name in ("adaptive", "animated_seed", "pixel_jitter", "denoise")) or any(settings.get(name) != value for name, value in (("samples", 128), ("seed", 0), ("frame", 1))):
            raise ValueError("Controlled sampler settings were not actually observed")
        completion = capture.get("actual_completion", {})
        generation, epoch = int(completion.get("generation", 0)), int(completion.get("epoch", 0))
        terminal = [event for event in events if event["generation"] == generation and event["epoch"] == epoch and event["sample"] == 128 and event["progress"] == 1 and "Rendering Done" in event["status"]]
        matching = [event for event in terminal if event["sequence"] == int(completion.get("sequence", 0))]
        if generation not in session_generations or generation < 1 or epoch < 1 or not matching or "Rendering Done" not in completion.get("status", "") or completion.get("sample") != 128 or completion.get("progress") != 1 or int(completion.get("terminal_epoch", 0)) != epoch or int(completion.get("draw_generation", 0)) != generation or int(completion.get("draw_epoch", 0)) != epoch:
            raise ValueError("Capture is not bound to actual terminal render/draw epoch")
        paint_begin, paint_done = int(completion.get("paint_begin_ns", 0)), int(completion.get("paint_done_ns", 0))
        if not min(event["steady_ns"] for event in terminal) < paint_begin <= paint_done:
            raise ValueError("Capture paint did not follow actual render completion")
        completions.append((generation, epoch))
        tiles = capture.get("full_tile_hashes", [])
        if {(tile.get("channel"), tile.get("tile")) for tile in tiles} != {(channel, tile) for channel in (0, 2) for tile in (1001, 1002)} or len(tiles) != 4:
            raise ValueError("Incomplete full GPU/CPU tile hashes")
        stage_hashes = {}
        for tile in tiles:
            digest = tile.get("gpu_sha256", "")
            if not re.fullmatch(r"[0-9a-f]{64}", digest) or tile.get("cpu_sha256") != digest or int(tile.get("bytes", 0)) != 2048 * 2048 * 16:
                raise ValueError("Full GPU/CPU tile byte identity failed")
            stage_hashes[(tile["channel"], tile["tile"])] = digest
        hashes[capture["stage"]] = stage_hashes
    baseline = report["captures"][0]
    if not isinstance(baseline.get("document"), dict) or report["captures"][1].get("document") != baseline["document"]:
        raise ValueError("No-op reload changed authored document JSON")
    color = baseline.get("controlled_settings", {}).get("color_management", {})
    if set(color) != {"view_transform", "look", "exposure", "gamma", "display_device", "dither", "working_space", "working_space_interop_id"} or any(capture["controlled_settings"].get("color_management") != color for capture in report["captures"]):
        raise ValueError("Missing or changed actual color-management settings")
    if any(completions[i][0] != completions[0][0] or completions[i][1] <= completions[i - 1][1] for i in range(1, 6)) or completions[-1][0] <= completions[0][0]:
        raise ValueError("Artist stages lack distinct reset/completion generations")
    if not hashes["cycles-before"] == hashes["cycles-noop-control"] == hashes["cycles-color-undone"] or not hashes["cycles-color-edited"] == hashes["cycles-color-redone"] == hashes["cycles-camera-only"]:
        raise ValueError("Full tile no-op/Undo/Redo restoration failed")


def require_displacement_attempt(runtime):
    """No old ROI/default report can substitute for this four-stage actor."""
    import math
    import re
    runtime = Path(runtime)
    log = (runtime / "run.log").read_text()
    report = json.loads((runtime / "verified.json").read_text())
    captures = report.get("captures", [])
    if (report.get("profile") != "live-displacement-controlled-v1" or
            [capture.get("stage") for capture in captures] != ["before", "edited", "undone", "redone"] or
            log.count("BTS_BA_DISPLACEMENT_DONE") != 1 or not re.search(r"\bBTS_SHUTDOWN clean(?:\s|$)", log)):
        raise ValueError("Displacement attempt lacks all stages/final gate/clean shutdown")
    trace = json.loads((runtime / "completion-trace.json").read_text())
    if trace.get("failed") or trace.get("failure"):
        raise ValueError("Displacement completion observer failed")
    sessions = trace.get("sessions", [])
    if len(sessions) != 1 or any(s.get("viewport") is not True or s.get("background") is not False or s.get("auto_tile") is not False for s in sessions):
        raise ValueError("Displacement lacks one actual viewport Session")
    generation = sessions[0].get("generation")
    previous_epoch = 0
    hashes = []
    for capture in captures:
        completion, geometry = capture.get("completion", {}), capture.get("geometry", {})
        epoch = completion.get("epoch", 0)
        terminal = any(event.get("generation") == generation and event.get("epoch") == epoch and
                       event.get("sample") == 128 and event.get("progress") == 1 and
                       "Rendering Done" in event.get("status", "") for event in trace.get("events", []))
        if (not terminal or completion.get("generation") != generation or epoch <= previous_epoch or
                completion.get("terminal_epoch") != epoch or completion.get("draw_epoch") != epoch or
                completion.get("sample") != 128 or completion.get("progress") != 1 or
                geometry.get("available") is not True or geometry.get("generation") != generation or geometry.get("epoch") != epoch):
            raise ValueError("Displacement capture is not current terminal/drawn geometry")
        previous_epoch = epoch
        tiles = capture.get("full_tiles", [])
        if len(tiles) != 2 or {tile.get("tile") for tile in tiles} != {1001, 1002}:
            raise ValueError("Displacement lacks both complete role16 tiles")
        stage_hashes = {}
        for tile in tiles:
            digest = tile.get("gpu_sha256", "")
            if not re.fullmatch(r"[0-9a-f]{64}", digest) or tile.get("cpu_sha256") != digest or tile.get("bytes") != 128 * 128 * 16:
                raise ValueError("Displacement GPU/CPU full-tile bytes differ")
            for suffix in ("gpu", "cpu"):
                raw = runtime / f"{capture['stage']}-{tile['tile']}-{suffix}.rgba32f"
                if raw.stat().st_size != tile["bytes"] or sha256(raw) != digest:
                    raise ValueError("Retained Displacement tile bytes differ")
            stage_hashes[tile["tile"]] = digest
        hashes.append(stage_hashes)
        oracle = json.loads((runtime / (capture["stage"] + "-geometry-oracle.json")).read_text())
        error = oracle.get("maximum_z_error", float("nan"))
        transverse = oracle.get("maximum_transverse_error", float("nan"))
        if (oracle.get("checked_vertices") != 24 or oracle.get("checked_transverse_vertices") != 2145 or
                oracle.get("bound") != 3e-5 or not math.isfinite(error) or not 0 <= error <= 3e-5 or
                not math.isfinite(transverse) or not 0 <= transverse <= 3e-5):
            raise ValueError("Displacement independent vertex oracle failed")
        for suffix in (".png", "-mesh-roi.png", "-workspace.png"):
            if (runtime / (capture["stage"] + suffix)).stat().st_size == 0:
                raise ValueError("Missing actual Displacement frame evidence")
    if hashes[0] != hashes[2] or hashes[1] != hashes[3] or hashes[0] == hashes[1]:
        raise ValueError("Displacement pattern edit/Undo/Redo full-tile restoration failed")
    if not (runtime / "painting-before.png").stat().st_size:
        raise ValueError("Missing pre-entry EEVEE frame evidence")
    return report
