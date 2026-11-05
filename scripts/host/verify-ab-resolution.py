"""Ten-minute native RTX run, full-resolution frame log and external VRAM pressure."""
import argparse
import os
import signal
from session_guard import user_sessions
import ctypes
import datetime
import statistics
import json
import multiprocessing
import re
import subprocess
import time
from pathlib import Path


def stop_private_host(out, proc=Path("/proc"), send=os.kill):
    """Only this benchmark's isolated native instance; never a user process."""
    stopped=[]
    for entry in proc.iterdir():
        if not entry.name.isdigit():continue
        try:
            if (entry/"comm").read_text().strip()!="bts-host":continue
            args=(entry/"cmdline").read_bytes().decode().rstrip("\0").split("\0")
            env=dict(v.split("=",1) for v in (entry/"environ").read_bytes().decode().split("\0") if "=" in v)
            if Path(env.get("XDG_CONFIG_HOME","/")).resolve()!=(out/"config").resolve():continue
            if "--gui-fix-demo" not in args:continue
            index=args.index("--gui-fix-demo")+1
            if index>=len(args) or Path(args[index]).resolve()!=(out/"workspace").resolve():continue
            send(int(entry.name),signal.SIGTERM);stopped.append(int(entry.name))
        except (OSError,ValueError,ProcessLookupError):continue
    return stopped


def pressure_worker(path, cancel):
    """Separate compute context, no surface, window or input device.

    A failed fixed-size alloc, or destroying this context while the host is
    still drawing, synchronizes the GL driver and shows up as a multi-hundred
    millisecond import ping. Size the alloc from free VRAM and keep the
    context until the host has exited.
    """
    cuda = ctypes.CDLL("libcuda.so.1")
    gib, mib = 1024**3, 1024**2
    target, reserve, alignment, minimum = 2 * gib, 1536 * mib, 2 * mib, 64 * mib

    def check(code):
        if code:
            raise RuntimeError(f"CUDA driver returned {code}")

    context = ctypes.c_void_p()
    device = ctypes.c_int()
    cuda.cuCtxCreate_v2.argtypes = [ctypes.POINTER(ctypes.c_void_p), ctypes.c_uint, ctypes.c_int]
    cuda.cuMemGetInfo_v2.argtypes = [ctypes.POINTER(ctypes.c_size_t), ctypes.POINTER(ctypes.c_size_t)]
    cuda.cuMemAlloc_v2.argtypes = [ctypes.POINTER(ctypes.c_uint64), ctypes.c_size_t]
    cuda.cuMemFree_v2.argtypes = [ctypes.c_uint64]
    cuda.cuCtxDestroy_v2.argtypes = [ctypes.c_void_p]
    check(cuda.cuInit(0))
    check(cuda.cuDeviceGet(ctypes.byref(device), 0))
    check(cuda.cuCtxCreate_v2(ctypes.byref(context), 0, device))
    allocation = ctypes.c_uint64()
    held = False
    try:
        with Path(path).open("w") as log:
            start = time.monotonic()
            for moment, allocate in [(120, True), (240, False), (360, True), (480, False)]:
                if cancel.wait(max(0, start + moment - time.monotonic())):
                    return
                allocated_bytes = 0
                free_bytes = None
                if allocate:
                    free, total = ctypes.c_size_t(), ctypes.c_size_t()
                    check(cuda.cuMemGetInfo_v2(ctypes.byref(free), ctypes.byref(total)))
                    free_bytes = int(free.value)
                    room = free_bytes - reserve if free_bytes > reserve else 0
                    size = min(target, room)
                    size -= size % alignment
                    if size >= minimum:
                        check(cuda.cuMemAlloc_v2(ctypes.byref(allocation), size))
                        held = True
                        allocated_bytes = size
                elif held:
                    check(cuda.cuMemFree_v2(allocation))
                    held = False
                log.write(json.dumps({"time": time.time(), "elapsed": time.monotonic() - start,
                                      "allocated_bytes": allocated_bytes, "free_bytes_at_decision": free_bytes}) + "\n")
                log.flush()
            cancel.wait()
    finally:
        if held:
            check(cuda.cuMemFree_v2(allocation))
        check(cuda.cuCtxDestroy_v2(context))


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--output", type=Path, required=True)
    p.add_argument("--project", type=Path, required=True)
    p.add_argument("--image", type=Path, required=True)
    p.add_argument("--seconds", type=int, default=620)
    a = p.parse_args()
    if user_sessions():
        print("BTS_AB_ENDURANCE_SKIPPED user session active; GPU retained for artist",flush=True)
        raise SystemExit(77)
    root = Path(__file__).resolve().parents[2]
    out, project, image = a.output.resolve(), a.project.resolve(), a.image.resolve()
    assert a.seconds >= 600
    for path in (out, project, image):
        assert path.is_relative_to(root.parent / "build")
    assert project.is_file() and image.is_file()
    out.mkdir(parents=True, exist_ok=True)
    env = dict(BTS_ROOT=str(root.parent), QT_QPA_PLATFORM="offscreen",
               BTS_HEADLESS_WORKSPACE="1", BTS_AA_PERFORMANCE="1",
               BTS_AB_RESOLUTION_SECONDS=str(a.seconds), BTS_RESOLUTION_LOG="1",
               BTS_PROPERTY_PERFORMANCE="1",
               BTS_AA_PROJECT=str(project), BTS_AA_IMAGE=str(image),
               LANG="sv_SE.UTF-8", LC_ALL="sv_SE.UTF-8", BTS_GPU_BRUSH_AUTHORING="1")
    # Detailed per-dispatch profiling is deliberately disabled in the endurance
    # run; the short matched benchmark measures latency without growing a log
    # of millions of micro-dispatch records.
    for key, folder in [("XDG_CONFIG_HOME", "config"), ("XDG_DATA_HOME", "data"),
                        ("XDG_CACHE_HOME", "cache"), ("BTS_RECOVERY_DIR", "recovery"),
                        ("BTS_RESOURCE_CACHE", "resources"), ("TMPDIR", "tmp"),
                        ("BTS_SESSION_LOG_DIR", "logs")]:
        (out / folder).mkdir(exist_ok=True)
        env[key] = str(out / folder)
    args = ["--workspace", "--open-document", str(project), "--gui-fix-demo", str(out / "workspace"),
            "--gui-fix-mesh", str(root / "native/tests/fixtures/two_tile_udim.obj"),
            "--gui-fix-resolution", "2048", "--autosave-seconds", "3600"]
    command = (['env'] if Path('/run/.containerenv').exists() else ['distrobox', 'enter', 'bts-build', '--', 'env'])
    command += [f"{k}={v}" for k, v in env.items()]
    command += ["nice", "-n", "19", "bash", str(root / "scripts/host/run-host.sh")] + args
    cancel_pressure = multiprocessing.Event()
    pressure = multiprocessing.Process(target=pressure_worker, args=(out / "pressure.jsonl", cancel_pressure))
    start = time.monotonic()
    pressure.start()
    result = None
    try:
        with (out / "run.log").open("w") as log:
            process = subprocess.Popen(command, stdout=log, stderr=subprocess.STDOUT,start_new_session=True)
            deadline=time.monotonic()+a.seconds+180
            while process.poll() is None:
                sessions=user_sessions()
                if sessions or time.monotonic()>deadline:
                    cancel_pressure.set()
                    stopped=stop_private_host(out)
                    if not stopped:
                        try:os.killpg(process.pid,signal.SIGTERM)
                        except ProcessLookupError:pass
                    try:process.wait(timeout=15)
                    except subprocess.TimeoutExpired:
                        stop_private_host(out)
                        try:os.killpg(process.pid,signal.SIGKILL)
                        except ProcessLookupError:pass
                        process.wait(timeout=15)
                    stop_private_host(out)
                    (out/"interrupted.json").write_text(json.dumps({"reason":"artist-session-started" if sessions else "timeout","user_sessions":sessions,"private_pids_stopped":stopped,"artist_touched":False},indent=2)+"\n")
                    raise RuntimeError("AB private benchmark stopped to protect artist session" if sessions else "AB benchmark timeout")
                try:process.wait(timeout=1)
                except subprocess.TimeoutExpired:pass
            result=subprocess.CompletedProcess(command,process.returncode)
        assert result.returncode == 0, result.returncode
    finally:
        # Wake the worker only after the host is gone so CUDA context teardown
        # cannot synchronize the viewport. On failure the host was already stopped.
        cancel_pressure.set()
        pressure.join()
    failures=[]
    if pressure.exitcode != 0:failures.append("External VRAM pressure worker failed")
    frames, editing, budgets, sources, visible = 0, 0, [], {}, {}
    caches=[]; texture_groups=[]; seen_events=set(); duplicate_presentations=0; first_visible={}
    with (out / "run.log").open() as log:
        for line in log:
            match = re.search(r"BTS_VIEWPORT_RESOLUTION frame=\d+ (\{.*\})", line)
            if match:
                state = json.loads(match[1])
                frames += 1
                editing += state["editing"]
                for item in state["images"]:
                    assert item["cap"] == item["sampling_cap"] == max(item["source_width"], item["source_height"]), item
                    assert not item["image"].startswith("BTS Edit Preview"), item
                    sizes = sources.setdefault(item["image"], set())
                    sizes.add((item["source_width"], item["source_height"], item["sampling_cap"]))
            cache=re.search(r"BTS_GPU_CACHE (.*)",line)
            if cache:caches.append({k:int(v) for k,v in re.findall(r"(\w+)=(\d+)",cache[1])})
            groups=re.search(r"BTS_GPU_TEXTURE_MEMORY (\[.*\])",line)
            if groups:texture_groups.append(json.loads(groups[1]))
            event = re.search(r"BTS_EDIT_MS property=(\S+) event=(\d+) .*?visible_ms=([0-9.]+)", line)
            if event:
                visible.setdefault(event[1], []).append(float(event[3]))
                if event[2] in seen_events:duplicate_presentations+=1
                else:seen_events.add(event[2]);first_visible.setdefault(event[1],[]).append(float(event[3]))
            match = re.search(r"BTS_RESIDENCY_VRAM total_kb=(\d+) free_kb=(\d+) available_ram=(\d+) automatic_budget=(\d+)", line)
            if match:
                value = dict(zip(("total_kb", "free_kb", "ram_bytes", "budget_bytes"), map(int, match.groups())))
                value["time"] = datetime.datetime.fromisoformat(line.split(" | ", 1)[0]).timestamp()
                budgets.append(value)
            assert not re.search(r"BTS_FINALIZE(?:_FALLBACK|_MS)|BTS_GPU_CPU_REFERENCE|phase=padding(?:\s|$)|BTS_DRAW_FAILED|BTS_HOST_FAILED", line), line.strip()
    elapsed = time.monotonic() - start
    assert elapsed >= a.seconds and frames > 1000 and editing > 100
    assert len(budgets) > 100
    pressure_records = [json.loads(x) for x in (out / "pressure.jsonl").read_text().splitlines()]
    allocated = [x["allocated_bytes"] for x in pressure_records]
    if len(allocated) != 4 or allocated[1] or allocated[3] or min(allocated[0], allocated[2]) < 64 * 1024**2:
        failures.append("Missing two external VRAM allocate/release cycles")
    for event in pressure_records:
        before = [x["free_kb"] for x in budgets if event["time"] - 30 < x["time"] < event["time"] - 5]
        after = [x["free_kb"] for x in budgets if event["time"] + 5 < x["time"] < event["time"] + 30]
        assert before and after, ("missing pressure query window", event)
        change = statistics.median(after) - statistics.median(before)
        if not (change < -1024**2 if event["allocated_bytes"] else change > 1024**2):failures.append(f"VRAM pressure response missing: {event}, change_kb={change}")
        event["measured_free_change_kb"] = change
    free_values = [x["free_kb"] for x in budgets]
    if max(free_values) - min(free_values) < 1024**2:failures.append("VRAM query did not reflect memory pressure")
    interactions = json.loads((out / "workspace/aa-performance.json").read_text())
    ping_by_flow = {}
    nonmodal_by_flow = {}
    for ping in interactions["ping_intervals"]:
        ping_by_flow.setdefault(ping["flow"], []).append(ping["interval_ms"])
        nonmodal_by_flow.setdefault(ping["flow"], []).append(ping["nonmodal_interval_ms"])
    def distribution(values):
        values = sorted(values)
        return {"count": len(values), "p95_ms": values[min(len(values)-1, int(len(values)*.95))], "max_ms": max(values)}
    if duplicate_presentations:failures.append(f"Invalid instrumentation: {duplicate_presentations} repeated first-presentation events")
    for flow,values in first_visible.items():
        if max(values)>33:failures.append(f"Next-frame latency exceeded33ms: {flow} max={max(values)}ms")
    import_nonmodal = nonmodal_by_flow.get("aa_import", [])
    if not import_nonmodal or max(import_nonmodal) > 50:
        failures.append(f"Import nonmodal ping exceeded 50ms: max={max(import_nonmodal) if import_nonmodal else None}ms")
    proof = {"goals_met":not failures,"failures":failures,"gpu_cache_samples":caches,"texture_groups":texture_groups,"seconds": elapsed, "frames": frames, "editing_frames": editing, "resolution_changes": 0,
             "source_sizes": {k: sorted(v) for k, v in sources.items()}, "budget_queries": budgets,
             "external_pressure": pressure_records, "no_low_resolution_placeholder": True,
             "visible_latency": {k: distribution(v) for k, v in first_visible.items()},
             "raw_visible_latency":{k:distribution(v) for k,v in visible.items()},"duplicate_presentations":duplicate_presentations,
             "ping_by_flow": {k: distribution(v) for k, v in ping_by_flow.items()},
             "nonmodal_ping_by_flow": {k: distribution(v) for k, v in nonmodal_by_flow.items()},
             "cpu_oracle_in_this_run": False, "cpu_finalize_or_padding": 0,
             "reference_scope": "Separate short matched GPU/CPU oracle benchmark; endurance has no CPU composition before, during, or after timing"}
    (out / "verified.json").write_text(json.dumps(proof, indent=2) + "\n")
    print(json.dumps({k: v for k, v in proof.items() if k not in ("budget_queries", "source_sizes","gpu_cache_samples","texture_groups")}, indent=2))
    assert not failures, failures


if __name__ == "__main__":
    main()
