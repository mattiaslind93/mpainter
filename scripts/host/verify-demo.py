"""Run the real host, inspect its process tree, and compare presented EEVEE frames."""
import argparse
import json
import math
import os
from pathlib import Path
import re
import subprocess
import time

from PIL import Image, ImageChops, ImageStat

parser = argparse.ArgumentParser()
parser.add_argument("--root", type=Path, default=Path("/home/mlind/bts"))
parser.add_argument("--output", type=Path, required=True)
parser.add_argument("--project", type=Path, help="Use another generated host fixture")
parser.add_argument("--open-document", type=Path, help="Open an editable project before camera captures (required for workspace navigation since the empty-project UI)")
parser.add_argument("--timeout", type=float, default=300)
mode = parser.add_mutually_exclusive_group()
mode.add_argument("--workspace", action="store_true", help="Verify the integrated Painter workspace")
mode.add_argument("--layouts", action="store_true", help="Verify the live 2D/3D layout sequence")
mode.add_argument("--navigation", action="store_true", help="Verify native camera gestures and projection")
args = parser.parse_args()
args.output.mkdir(parents=True, exist_ok=True)
project = Path(__file__).resolve().parents[2]
command = ["bash", str(project / "scripts/host/run-host.sh"), "--project",
           str(args.project or args.root / "build/baseline-demo/host-fixture.blend"),
           "--navigation-demo-dir" if args.navigation else "--layout-demo-dir" if args.layouts else "--demo-dir", str(args.output)]
if args.workspace:
    command.append("--workspace")
if args.open_document:
    command.extend(["--open-document",str(args.open_document)])


def descendants(pid):
    found = set()
    pending = [pid]
    while pending:
        parent = pending.pop()
        for children in Path(f"/proc/{parent}/task").glob("*/children"):
            try:
                current = {int(value) for value in children.read_text().split()}
            except FileNotFoundError:
                continue
            for child in current - found:
                found.add(child)
                pending.append(child)
    return found


observed_children = {}
started = time.monotonic()
with (args.output / "run.log").open("w") as log:
    process = subprocess.Popen(command, stdout=log, stderr=subprocess.STDOUT,
                               env={**os.environ, "BTS_ROOT": str(args.root)})
    while process.poll() is None:
        try:
            host_running = Path(f"/proc/{process.pid}/exe").resolve(strict=True).name == "bts-host"
        except FileNotFoundError:
            host_running = False
        for pid in descendants(process.pid) if host_running else ():
            try:
                observed_children[pid] = Path(f"/proc/{pid}/cmdline").read_bytes().replace(b"\0", b" ").decode()
            except (FileNotFoundError, ProcessLookupError):
                pass
        if time.monotonic() - started > args.timeout:
            process.terminate()
            try:
                process.wait(timeout=15)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
            raise SystemExit(f"Host timed out; see {args.output / 'run.log'}")
        time.sleep(0.1)
    if process.returncode:
        raise SystemExit(f"Host exited {process.returncode}; see {args.output / 'run.log'}")

log = (args.output / "run.log").read_text()
host = json.loads((args.output / ("navigation.json" if args.navigation else "layout.json" if args.layouts else "host.json")).read_text())
assert host["pid"] == process.pid
markers = ["BTS_SCENE_READY", "BTS_GPU_READY", "BTS_FRAME", "BTS_SHUTDOWN clean"]
if not args.navigation and not args.open_document:
    markers.append("BTS_MATERIAL_EDIT")
for marker in markers:
    lines = [line for line in log.splitlines() if line.startswith(marker)]
    assert lines, f"Missing native marker: {marker}"
    assert all(int(re.search(r"pid=(\d+)", line)[1]) == process.pid for line in lines)
assert "3090" in host["renderer"]
assert host["qt"].startswith("6.8.")
assert host["frames"] >= (30 if args.layouts else 12)
if not args.layouts and not args.navigation:
    assert host.get("workspace", False) == args.workspace
assert not observed_children, f"Host spawned child processes: {observed_children}"
assert "Not freed memory blocks" not in log, "Blender reported leaked allocations on exit"

def difference(first, second):
    assert first.size == second.size
    return sum(ImageStat.Stat(ImageChops.difference(first, second)).mean) / (3 * 255)


images = {}
presentation_error = {}
rectangles = {capture["stage"]: capture["viewport_rect"] for capture in host["captures"]}
for name in rectangles:
    images[name] = Image.open(args.output / f"{name}-viewport.png").convert("RGB")
    assert max(ImageStat.Stat(images[name]).stddev) > 8, f"Blank {name} viewport"
    window = Image.open(args.output / f"{name}-window.png").convert("RGB")
    assert window.width >= images[name].width and window.height >= images[name].height
    assert max(ImageStat.Stat(window).stddev) > 8, f"Blank {name} presentation"
    x, y, width, height = rectangles[name]
    presented_viewport = window.crop((x, y, x + width, y + height))
    presentation_error[name] = difference(images[name], presented_viewport)
    assert presentation_error[name] < 0.02, f"EEVEE pixels were not presented: {name}"


changes = {}
if args.navigation:
    expected = ["initial", "orbit", "snap-orbit", "pan", "dolly", "wheel", "frame",
                "orthographic", "perspective", "split", "resize", "plain-drag", "uv-pan",
                "uv-wheel", "pane-capture", "gesture-capture", "plain-then-alt",
                "released-gesture", "frame-split", "projection-selector"]
    assert [c["stage"] for c in host["captures"]] == expected
    states = {c["stage"]: c["view_state"] for c in host["captures"]}
    scene_keys = ("scene_center", "scene_yaw", "scene_pitch", "scene_distance",
                  "scene_vertical_extent", "scene_projection")
    uv_keys = ("uv_center", "uv_vertical_extent")
    initial = host["captures"][0]
    for capture in host["captures"]:
        state = capture["view_state"]
        assert capture["document_matches_initial"]
        assert capture["document_state"] == initial["document_state"]
        assert state["active_gesture"] == "Idle"
        assert capture["workspace_state"]["camera_projection"] == state["scene_projection"]
        assert state["selected_material_session_id"] == initial["view_state"]["selected_material_session_id"]
    # Opening a document initializes its materials before the first navigation capture.
    navigation_log=log.split("BTS_NAVIGATION_DEMO stage=initial",1)[1] if args.open_document else log
    assert "BTS_MATERIAL_EDIT" not in navigation_log
    for before, after in [("initial", "orbit"), ("orbit", "snap-orbit"),
                          ("pane-capture", "gesture-capture")]:
        assert states[before]["scene_yaw"] != states[after]["scene_yaw"]
    for key in ("scene_yaw", "scene_pitch"):
        assert math.isclose(states["snap-orbit"][key] % host["snap_degrees"], 0, abs_tol=1e-6)
    assert states["pan"]["scene_center"] != states["snap-orbit"]["scene_center"]
    assert states["dolly"]["scene_distance"] > states["pan"]["scene_distance"]
    assert states["wheel"]["scene_distance"] < states["dolly"]["scene_distance"]
    for name in ("frame", "frame-split"):
        state = states[name]
        bounds = state["last_framed_bounds"]
        assert bounds["mesh_objects"] > 0
        pane = next(p for p in state["panes"] if p["view"] == "3D")
        assert bounds["pane_size"] == pane["rect"][2:]
        expected_center = [(a+b)/2 for a,b in zip(bounds["minimum"], bounds["maximum"])]
        assert all(math.isclose(a,b,abs_tol=1e-5) for a,b in zip(state["scene_center"],expected_center))
    assert states["orthographic"]["scene_projection"] == "Orthographic view"
    assert states["perspective"]["scene_projection"] == "Perspective view"
    assert states["projection-selector"]["scene_projection"] == "Orthographic view"
    for name in ("orthographic", "perspective"):
        assert math.isclose(states[name]["scene_vertical_extent"], states["frame"]["scene_vertical_extent"])
    for before, after in [("resize", "plain-drag"), ("gesture-capture", "plain-then-alt"),
                          ("plain-then-alt", "released-gesture")]:
        for key in scene_keys + uv_keys:
            assert states[before][key] == states[after][key], (after, key)
    for before, after in [("plain-drag", "uv-pan"), ("uv-pan", "uv-wheel")]:
        for key in scene_keys:
            assert states[before][key] == states[after][key], (after, key)
    assert states["uv-pan"]["uv_center"] != states["plain-drag"]["uv_center"]
    assert states["uv-wheel"]["uv_vertical_extent"] > states["uv-pan"]["uv_vertical_extent"]
    for key in uv_keys:
        assert states["pane-capture"][key] == states["uv-wheel"][key]
    assert states["pane-capture"]["scene_center"] != states["uv-wheel"]["scene_center"]
    for before, after in [("initial", "orbit"), ("frame", "orthographic")]:
        change = difference(images[before], images[after])
        assert change > 0.002, f"Navigation did not change EEVEE pixels: {after}"
        changes[after + "_difference"] = change
elif args.layouts:
    expected = ["3D only", "3D/2D", "2D/3D", "3D only", "3D only", "2D only",
                "2D only", "2D/3D", "2D/3D", "2D/3D"]
    assert len(host["captures"]) == len(expected)
    states = {capture["stage"]: capture for capture in host["captures"]}
    initial = host["captures"][0]
    for capture, layout in zip(host["captures"], expected):
        state = capture["view_state"]
        workspace = capture["workspace_state"]
        assert state["layout"] == layout
        assert workspace["viewport_layout"] == layout, "Toolbar lost the active layout"
        assert workspace["texture_sets"] == (initial["workspace_state"]["texture_sets"] if args.open_document else ["Copper"])
        assert workspace["selected_material_session_id"] == initial["workspace_state"]["selected_material_session_id"]
        assert state["selected_material_session_id"] == workspace["selected_material_session_id"]
        for key in ("scene_center", "scene_yaw", "scene_pitch", "scene_distance",
                    "scene_vertical_extent", "uv_center", "uv_vertical_extent"):
            assert state[key] == initial["view_state"][key], f"Layout changed {key}"
        panes = state["panes"]
        assert [pane["view"] for pane in panes] == layout.replace(" only", "").split("/")
        for pane in panes:
            x, y, width, height = pane["rect"]
            pixels = images[capture["stage"]].crop((x, y, x + width, y + height))
            assert max(ImageStat.Stat(pixels).stddev) > 8, f"Blank {pane['view']} pane"
            if pane["view"] == "2D":
                assert state["uv_triangles"] > 0, "UV view has no native geometry"
    assert host["roughness_edit_applied"]
    assert abs(states["roughness"]["workspace_state"]["roughness"] - 0.8) < 0.001
    for before, after in zip(states["resize"]["view_state"]["panes"],
                             states["roughness"]["view_state"]["panes"]):
        assert before["rect"] == after["rect"]
        x, y, width, height = before["rect"]
        rectangle = (x, y, x + width, y + height)
        change = difference(images["resize"].crop(rectangle), images["roughness"].crop(rectangle))
        if before['view']=='3D':
            assert change > 0.002, "Roughness did not affect the lit 3D material"
        else:
            assert change < 0.0001, "Roughness lighting leaked into the flat Base Color UV view"
        changes[before["view"] + "_roughness_difference"] = change
    assert images["resize"].width > images["split-restored"].width
else:
    roughness_change = difference(images["initial"], images["roughness"])
    orbit_change = difference(images["roughness"], images["orbit"])
    assert roughness_change > 0.002, "Roughness did not change the EEVEE pixels"
    assert orbit_change > 0.01, "Orbit did not change the EEVEE pixels"
    assert images["resize"].width > images["initial"].width
    changes = {"roughness_difference": roughness_change, "orbit_difference": orbit_change}
if args.workspace:
    workspace = host["workspace_state"]
    assert workspace["texture_sets"] == ["Copper"], "Workspace did not read the fixture's real material"
    assert workspace["selected_texture_set"] == 0
    assert abs(workspace["roughness"] - 0.8) < 0.001
    assert workspace["settings_tab"] == 1
    assert workspace["console_visible"] is False
result = {**host, **changes, "children": observed_children, "presentation_error": presentation_error,
          "clean_exit": True,
          "scope": ("PD-118 native camera navigation on Linux OpenGL" if args.navigation else
                    "PD-117 live EEVEE 2D/3D layouts on Linux OpenGL" if args.layouts else
                    "APP-001 integrated Qt workspace on Linux OpenGL" if args.workspace else
                    "HOST-001 Linux OpenGL; not other-platform or painting acceptance")}
(args.output / "verified.json").write_text(json.dumps(result, indent=2) + "\n")
print(json.dumps({k:v for k,v in result.items() if k != "captures"}, indent=2))
