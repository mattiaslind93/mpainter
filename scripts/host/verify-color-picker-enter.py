"""Actual ColorPicker CPU regression against immutable native link inputs."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shlex
import subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument("--snapshot", type=Path, required=True)
p.add_argument("--generated-headers", type=Path, required=True, help="Immutable generated-header directory retained with the build proof")
p.add_argument("--output", type=Path, required=True)
p.add_argument("--baseline", action="store_true", help="Prove the old exact source activates Previous; expected failure")
a = p.parse_args()
repo = Path(__file__).resolve().parents[2]
bts = repo.parent
snapshot, generated, out = a.snapshot.resolve(), a.generated_headers.resolve(), a.output.resolve()
if not out.is_relative_to(bts / "build") or out.is_relative_to(snapshot) or out.is_relative_to(bts / "build/host"):
    raise ValueError("Use an independent private build output")
out.mkdir(parents=True, exist_ok=True)
(out / "bin").mkdir(exist_ok=True)
commands = json.loads((snapshot / "host-commands.json").read_text())
env = dict(os.environ, CCACHE_DIR=str(bts / "build/ccache"))


def digest(path):
    result = hashlib.sha256()
    with Path(path).open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            result.update(block)
    return result.hexdigest()


def compilation(name):
    return next(shlex.split(c) for c in commands if " -c " in c and c.split(" -c ")[-1].strip().endswith("/native/host/" + name))


def call(args, label):
    (out / (label + ".args.json")).write_text(json.dumps(args, indent=2))
    with (out / (label + ".log")).open("w") as log:
        subprocess.run(["nice", "-n", "19", *args], cwd=snapshot, env=env, stdout=log, stderr=subprocess.STDOUT, check=True)


sources = {"ColorPicker.cc": repo / "native/host/ColorPicker.cc", "NativeMaterialPanelTest.cc": repo / "native/harness/NativeMaterialPanelTest.cc", "main.cc": repo / "native/tests/color_picker_enter_test.cc"}
if a.baseline:
    baseline = json.loads((snapshot / "snapshot.json").read_text())["source_head"]
    original = subprocess.check_output(["git", "show", baseline + ":native/host/ColorPicker.cc"], cwd=repo)
    sources["ColorPicker.cc"] = out / "ColorPicker-baseline.cc"
    sources["ColorPicker.cc"].write_bytes(original)
source_hashes = {str(v): digest(v) for v in (*sources.values(), repo / "native/host/ColorPicker.hh", repo / "native/harness/ColorPickerTest.hh")}
replacements = {}
for name, source in sources.items():
    args = compilation(name)
    original_object = args[args.index("-o") + 1]
    source_root = args[args.index("-c") + 1].split("/native/")[0]
    args = [v.replace(source_root + "/native/host", str(repo / "native/host")) for v in args]
    args[2:2] = ["-I" + str(generated / "source/creator"), "-I" + str(generated / "source/blender/makesrna")]
    obj = out / (name + ".o")
    args[args.index("-c") + 1] = str(source)
    args[args.index("-o") + 1] = str(obj)
    for flag, value in (("-MF", str(obj) + ".d"), ("-MT", str(obj))):
        if flag in args:
            args[args.index(flag) + 1] = value
    call(args, name + ".compile")
    replacements[original_object] = str(obj)
link = shlex.split(commands[-1].split(" && ")[1])
link = [replacements.get(v, v) for v in link]
binary = out / "bin/color-picker-enter"
link[link.index("-o") + 1] = str(binary)
link = [v.replace(str(bts / "build/host/bin/lib"), str(snapshot / "bin/lib")) for v in link]
call(link, "link")
original_main = compilation("main.cc")
original_main = original_main[original_main.index("-o") + 1]
ordinary = [original_main if v == replacements[original_main] else v for v in link]
ordinary[ordinary.index("-o") + 1] = str(out / "bin/bts-host")
call(ordinary, "ordinary.link")
for resource in (bts / "build/host/bin").iterdir():
    if resource.is_dir() and resource.name[:1].isdigit():
        target = out / "bin" / resource.name
        if not target.exists():
            target.symlink_to(resource)
runtime = out / "run"
runtime.mkdir(exist_ok=False)
for key, folder in (("XDG_CONFIG_HOME", "config"), ("XDG_CACHE_HOME", "cache"), ("XDG_DATA_HOME", "data"), ("XDG_STATE_HOME", "state"), ("BTS_RECOVERY_DIR", "recovery"), ("BTS_RESOURCE_CACHE", "resources"), ("TMPDIR", "tmp")):
    (runtime / folder).mkdir()
    env[key] = str(runtime / folder)
env.update(QT_QPA_PLATFORM="offscreen", BTS_HEADLESS_WORKSPACE="1", BTS_HOST_EXECUTABLE=str(binary), QT_PLUGIN_PATH=str(bts / "qt/6.8.3/plugins"), LD_LIBRARY_PATH=str(bts / "qt/6.8.3/lib") + ":" + str(snapshot / "bin/lib"))
with (runtime / "run.log").open("w") as log:
    result = subprocess.run(["nice", "-n", "19", "bash", str(repo / "scripts/host/run-host.sh"), "--no-splash"], cwd=repo, env=env, stdout=log, stderr=subprocess.STDOUT, timeout=90)
text = (runtime / "run.log").read_text()
if a.baseline:
    if result.returncode != 1 or "PREVIOUS_CLICK default=1 auto_default=1" not in text or "Enter activated Previous" not in text:
        raise RuntimeError("Original source did not reproduce the expected Enter/Previous failure")
elif result.returncode:
    raise RuntimeError("ColorPicker CPU regression failed; see " + str(runtime / "run.log"))
if not a.baseline:
    env["BTS_HOST_EXECUTABLE"] = str(out / "bin/bts-host")
    env.pop("BTS_NATIVE_MATERIAL_CASES", None)
    with (runtime / "ordinary-cpu.log").open("w") as log:
        subprocess.run(["nice", "-n", "19", "bash", str(repo / "scripts/host/run-host.sh"), "--no-splash", "--cpu-native-material-test", str(runtime / "panel")], cwd=repo, env=env, stdout=log, stderr=subprocess.STDOUT, timeout=90, check=True)
for source, expected in source_hashes.items():
    if digest(source) != expected:
        raise RuntimeError("Source changed during ColorPicker compilation/test: " + source)
proof = {"baseline_expected_failure": a.baseline, "gpu_context": False, "actual_color_picker": True, "ordinary_cpu_dispatcher": "--cpu-native-material-test", "ordinary_cpu_pass": not a.baseline, "source_sha256": source_hashes, "binary_sha256": digest(binary), "ordinary_host_sha256": digest(out / "bin/bts-host"), "snapshot_manifest_sha256": digest(snapshot / "snapshot.json"), "run_log_sha256": digest(runtime / "run.log"), "exit_code": result.returncode}
(out / "verified.json").write_text(json.dumps(proof, indent=2))
print("Actual ColorPicker CPU", "original negative" if a.baseline else "fixed", "PASS", runtime, flush=True)
