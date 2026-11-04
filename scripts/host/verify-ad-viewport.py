"""Offscreen viewport stroke p95 at 4K and 8K, plus the embedded UV preview edit."""
import argparse
import json
import os
import re
import signal
import subprocess
import time
from pathlib import Path

from session_guard import user_sessions


def stop_private_host(out, proc=Path("/proc"), send=os.kill):
    stopped = []
    for entry in proc.iterdir():
        if not entry.name.isdigit():
            continue
        try:
            if (entry / "comm").read_text().strip() != "bts-host":
                continue
            args = (entry / "cmdline").read_bytes().decode().rstrip("\0").split("\0")
            env = dict(v.split("=", 1) for v in (entry / "environ").read_bytes().decode().split("\0") if "=" in v)
            if Path(env.get("XDG_CONFIG_HOME", "/")).resolve() != (out / "config").resolve():
                continue
            if "--gui-fix-demo" not in args:
                continue
            send(int(entry.name), signal.SIGTERM)
            stopped.append(int(entry.name))
        except (OSError, ValueError, ProcessLookupError):
            continue
    return stopped


def percentile(values):
    values = sorted(values)
    return {"count": len(values), "p95_ms": values[min(len(values) - 1, int(len(values) * 0.95))], "max_ms": max(values)}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if user_sessions():
        print("BTS_AD_VIEWPORT_SKIPPED user session active", flush=True)
        raise SystemExit(77)
    root = Path(__file__).resolve().parents[2]
    out = args.output.resolve()
    mesh = (root / "fixtures/two_tile_udim.obj").resolve()
    assert out.is_relative_to(root.parent / "build") and mesh.is_file()
    out.mkdir(parents=True, exist_ok=True)
    start = time.monotonic()
    # A fresh project at each size. Resizing a painted 4K set to 8192 is a different edit and is not the stroke.
    log_path = out / "run.log"
    log_path.write_text("")
    uv_proofs = {}
    for resolution in (4096, 8192):
        run = out / str(resolution)
        env = dict(BTS_ROOT=str(root.parent), QT_QPA_PLATFORM="offscreen", BTS_HEADLESS_WORKSPACE="1",
                   BTS_AD_STROKE_BENCH="1", BTS_GPU_BRUSH_AUTHORING="1", LANG="sv_SE.UTF-8", LC_ALL="sv_SE.UTF-8")
        for key, folder in [("XDG_CONFIG_HOME", "config"), ("XDG_DATA_HOME", "data"), ("XDG_CACHE_HOME", "cache"),
                            ("BTS_RECOVERY_DIR", "recovery"), ("BTS_RESOURCE_CACHE", "resources"), ("TMPDIR", "tmp"),
                            ("BTS_SESSION_LOG_DIR", "logs")]:
            (run / folder).mkdir(parents=True, exist_ok=True)
            env[key] = str(run / folder)
        command = ["distrobox", "enter", "bts-build", "--", "env", *[f"{k}={v}" for k, v in env.items()],
                   "nice", "-n", "19", "bash", str(root / "scripts/host/run-host.sh"),
                   "--workspace", "--gui-fix-demo", str(run / "workspace"), "--gui-fix-mesh", str(mesh),
                   "--gui-fix-resolution", str(resolution), "--autosave-seconds", "3600"]
        with log_path.open("a") as log:
            log.write(f"\nBTS_AD_VIEWPORT_RUN resolution={resolution}\n")
            process = subprocess.Popen(command, stdout=log, stderr=subprocess.STDOUT, start_new_session=True)
            deadline = time.monotonic() + 900
            while process.poll() is None:
                if user_sessions() or time.monotonic() > deadline:
                    stop_private_host(run)
                    process.wait(timeout=20)
                    raise SystemExit(f"AD viewport bench stopped at {resolution}")
                try:
                    process.wait(timeout=1)
                except subprocess.TimeoutExpired:
                    pass
        assert process.returncode == 0, (resolution, process.returncode)
        uv_proofs[str(resolution)] = json.loads((run / "workspace/uv-preview-edit.json").read_text())
    text = log_path.read_text(errors="replace")
    assert "Spatial history limit reached" not in text
    assert "BTS_BRUSH_CPU_FALLBACK" not in text
    assert "BTS_FINALIZE" not in text and "BTS_GPU_CPU_REFERENCE" not in text
    groups = {}
    current = None
    for line in text.splitlines():
        match = re.search(r"BTS_AD_STROKE_RES resolution=(\d+) tool=(\w+)", line)
        if match:
            current = (int(match[1]), match[2])
            continue
        wall = re.search(r"BTS_STROKE_MS wall_ms=([0-9.]+)", line)
        if wall and current:
            groups.setdefault(current, []).append(float(wall[1]))
            current = None
    summary = {}
    failures = []
    for resolution in (4096, 8192):
        for tool in ("paint", "smudge", "erase", "clone"):
            values = groups.get((resolution, tool), [])
            if len(values) < 8:
                failures.append(f"missing samples {resolution} {tool} n={len(values)}")
                continue
            stats = percentile(values)
            summary[f"{resolution}:{tool}"] = stats
            if stats["p95_ms"] > 33:
                failures.append(f"stroke p95 {resolution} {tool} {stats['p95_ms']} ms")
    for resolution, uv in uv_proofs.items():
        if uv["integrated_marker_pixels"] < 4 or uv["after_centroid_x"] <= uv["before_centroid_x"] + 15:
            failures.append(f"uv preview {resolution}")
    proof = {"goals_met": not failures, "failures": failures, "seconds": time.monotonic() - start,
             "viewport_stroke_ms": summary, "uv_preview_edit": uv_proofs,
             "limit_reached": False, "cpu_brush_fallback": False}
    (out / "verified.json").write_text(json.dumps(proof, indent=2) + "\n")
    print(json.dumps(proof, indent=2))
    assert not failures, failures


if __name__ == "__main__":
    main()
