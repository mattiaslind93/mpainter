"""Height policy's two test actors on a fully rebuilt immutable production snapshot.

This module does not refresh production objects or replay fork patches.  It uses
the existing integration collector for production/header admission only.
"""
import json
import math
import os
from pathlib import Path
import re
import shlex
import shutil
import struct
import subprocess
import tempfile
import time

import cycles_integrated_snapshot as integration
from cycles_live_manifest import sha256

PROFILE = "native-height-policy-integrated-v1"
ACTORS = {"owner": ("native_height_policy_native_test.cc", "main.cc"),
          "artist": ("native_height_policy_artist_test.cc", "GuiFixDemo.cc")}
RUNNERS = tuple("scripts/host/" + name for name in (
    "native_height_verifier.py", "verify-native-height-policy.py",
    "cycles_integrated_snapshot.py", "cycles_live_manifest.py", "run-host.sh",
    "gpu-env.sh", "session_logging.py"))


def require(ok, message):
    if not ok:
        raise ValueError(message)


def write_json(path, value):
    Path(path).write_text(json.dumps(value, indent=2) + "\n")


def source_identity(repo):
    return {name: sha256(repo / name) for name in RUNNERS}


def relocate_include(value, mappings):
    # Boundary-aware and longest first: the repo may live beneath the Blender
    # prefix. Preserve flags and original ordering, including split -isystem.
    for prefix, target in sorted(mappings, key=lambda item: -len(item[0])):
        for flag in ("-I", "-isystem", "-iquote", ""):
            name = value[len(flag):] if flag and value.startswith(flag) else value if not flag else None
            if name is not None and (name == prefix or name.startswith(prefix + "/")):
                return flag + target + name[len(prefix):]
    return value


def actor_link(commands, admitted, snapshot, out, actor, obj):
    args = integration.link_arguments(commands)
    original = ACTORS[actor][1]
    matches = [value for value in args if value.endswith("/native/host/" + original + ".o")]
    require(len(matches) == 1, "Expected exactly one production replacement: " + original)
    replacement = matches[0]
    result = []
    for value in args:
        if value == replacement:
            value = str(obj)
        elif value in admitted["link_inputs"]:
            info = admitted["link_inputs"][value]
            value = str(snapshot / info["path"]) if info["kind"] == "frozen" else info["path"]
        elif value in admitted["linker_scripts"]:
            value = "-Wl,--version-script=" + str(snapshot / "integration-blender" / admitted["linker_scripts"][value])
        elif value.startswith("-Wl,--dependency-file="):
            value = "-Wl,--dependency-file=" + str(out / (actor + ".link.d"))
        else:
            value = value.replace(admitted["source_build"] + "/bin/lib", str(snapshot / "bin/lib"))
        result.append(value)
    result[result.index("-o") + 1] = str(out / "bin" / ("height-policy-" + actor))
    return result, replacement


def build(snapshot, repo, out, env):
    admitted = integration.validate(snapshot, repo)
    require(not (out / "build.json").exists(), "Choose a fresh build output; preserve earlier attempts")
    sources = snapshot / "integration-source"
    require(all((sources / "native/tests" / name).is_file() for name, _ in ACTORS.values()),
            "Matching full snapshot must include both merged Height actors")
    runners = source_identity(repo)
    commands = json.loads((snapshot / "host-commands.json").read_text())
    integration.native_units(commands)  # Reject incomplete native compile metadata.
    original_commands = {}
    for actor, (_, original) in ACTORS.items():
        selected = [shlex.split(command) for command in commands if " -c " in command and
                    shlex.split(command)[shlex.split(command).index("-c") + 1].endswith("/native/host/" + original)]
        require(len(selected) == 1, "Missing exact production compile template: " + original)
        original_commands[actor] = selected[0]
    for folder in ("objects", "bin"):
        (out / folder).mkdir(parents=True, exist_ok=True)

    def call(args, label):
        with (out / (label + ".log")).open("w") as log:
            subprocess.run(["nice", "-n", "19", *args], cwd=snapshot, env=env,
                           stdout=log, stderr=subprocess.STDOUT, check=True)

    discovery = {}
    for actor, (name, _) in ACTORS.items():
        args = original_commands[actor]
        native_root = args[args.index("-c") + 1].split("/native/", 1)[0] + "/native"
        args = [relocate_include(v, [(native_root, str(sources / "native"))]) for v in args]
        args[args.index("-c") + 1] = str(sources / "native/tests" / name)
        args += ["-I" + str(sources / "native/host"), "-g0"]
        discovery[name] = args
    supplement = integration.freeze_test_headers(snapshot, out, admitted, discovery, call,
                                                  sources, admitted["native_source_sha256"])
    allowed = {str(sources / name): digest for name, digest in admitted["native_source_sha256"].items()}
    allowed.update({str(snapshot / "integration-generated" / name): digest for name, digest in admitted["generated_sha256"].items()})
    allowed.update({str(out / "test-headers/blender" / name): digest for name, digest in supplement["production_context_sha256"].items()})
    allowed.update({str(out / "test-headers/blender" / name): info["sha256"] for name, info in supplement["headers"].items()})
    allowed.update(supplement["external_sha256"])
    records, links = [], []
    frozen_commands = {}
    for actor, (name, _) in ACTORS.items():
        obj = out / "objects" / (actor + ".o")
        mappings = [(admitted["blender_root"], str(out / "test-headers/blender")),
                    (admitted["source_build"], str(snapshot / "integration-generated"))]
        args = [relocate_include(value, mappings) for value in discovery[name]]
        args[args.index("-o") + 1] = str(obj)
        for flag, value in (("-MF", str(obj) + ".d"), ("-MT", str(obj))):
            if flag in args:
                args[args.index(flag) + 1] = value
        dep = out / (actor + ".frozen.d")
        scan = integration.dependency_scan(args, dep)
        call(scan, actor + ".frozen-discover")
        for dependency in integration.make_dependencies(dep):
            path = integration.dependency_path(str(dependency), snapshot)
            require(allowed.get(str(path)) == sha256(path), "Unadmitted final test dependency: " + str(path))
        frozen_commands[actor] = (args, obj, scan, sha256(dep))
    # Both complete frozen preprocessor scans precede either compilation.
    for actor, (name, _) in ACTORS.items():
        args, obj, scan, dep_sha = frozen_commands[actor]
        call(args, actor + ".compile")
        records.append({"actor": actor, "source": "native/tests/" + name,
                        "source_sha256": sha256(sources / "native/tests" / name),
                        "object": str(obj), "object_sha256": sha256(obj), "command": args,
                        "template": original_commands[actor], "scan": scan, "scan_sha256": dep_sha})
        command, replaced = actor_link(commands, admitted, snapshot, out, actor, obj)
        call(command, actor + ".link")
        binary = out / "bin" / ("height-policy-" + actor)
        links.append({"actor": actor, "binary": str(binary), "sha256": sha256(binary),
                      "command": command, "replaced_production_object": replaced})
    integration.validate(snapshot, repo)
    integration.validate_test_headers(snapshot, out, supplement)
    require(runners == source_identity(repo), "Runner source changed during build")
    require((snapshot / "bin/5.2").is_dir(), "Snapshot lacks independent runtime resources")
    (out / "bin/5.2").symlink_to(snapshot / "bin/5.2")
    record = {"schema": 1, "profile": PROFILE, "snapshot": str(snapshot),
              "snapshot_manifest_sha256": sha256(snapshot / "integrated-snapshot.json"),
              "built_source_head": admitted["native_source_head"],
              "runner_head": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=repo, text=True).strip(),
              "runner_sha256": runners, "compiled_test_units": records, "links": links,
              "production_units_recompiled": [], "fork_patches_reapplied": False,
              "header_manifest_sha256": sha256(out / "test-headers.json")}
    write_json(out / "build.json", record)
    write_json(out / "run-manifest.json", {"schema": 1, "profile": PROFILE,
               "build_sha256": sha256(out / "build.json")})


def validate_build(snapshot, repo, out):
    manifest = json.loads((out / "run-manifest.json").read_text())
    require(manifest == {"schema": 1, "profile": PROFILE, "build_sha256": sha256(out / "build.json")},
            "Stale Height run manifest")
    record = json.loads((out / "build.json").read_text())
    require(record.get("schema") == 1 and record.get("profile") == PROFILE and
            Path(record["snapshot"]).resolve() == snapshot.resolve() and
            record.get("snapshot_manifest_sha256") == sha256(snapshot / "integrated-snapshot.json"), "Wrong full production parent")
    admitted = integration.validate(snapshot, repo)
    require(record.get("built_source_head") == admitted["native_source_head"], "Production source head differs")
    require(record.get("runner_sha256") == source_identity(repo), "Stale Height runner source")
    require(record.get("production_units_recompiled") == [] and record.get("fork_patches_reapplied") is False,
            "Height verifier must never refresh production or replay patches")
    require(record.get("header_manifest_sha256") == sha256(out / "test-headers.json"), "Stale Height header receipt")
    integration.validate_test_headers(snapshot, out, json.loads((out / "test-headers.json").read_text()))
    units, links = record.get("compiled_test_units", []), record.get("links", [])
    require([row.get("actor") for row in units] == list(ACTORS) and [row.get("actor") for row in links] == list(ACTORS),
            "Height build requires exactly two test TUs and two binaries")
    commands = json.loads((snapshot / "host-commands.json").read_text())
    for unit, link in zip(units, links):
        actor = unit["actor"]
        require(unit["source"] == "native/tests/" + ACTORS[actor][0] and
                sha256(repo / unit["source"]) == unit["source_sha256"] and
                Path(unit["object"]).resolve() == (out / "objects" / (actor + ".o")).resolve() and
                sha256(unit["object"]) == unit["object_sha256"], "Stale Height test source/object")
        require(sha256(out / (actor + ".frozen.d")) == unit["scan_sha256"], "Stale final test dependency scan")
        require(unit["command"][unit["command"].index("-c") + 1] == str(snapshot / "integration-source" / unit["source"]) and
                unit["command"][unit["command"].index("-o") + 1] == unit["object"], "Wrong test compile input/output")
        expected, replacement = actor_link(commands, admitted, snapshot, out, actor, Path(unit["object"]))
        require(link["command"] == expected and link["replaced_production_object"] == replacement,
                "Wrong production object replacement")
        require(Path(link["binary"]).resolve() == (out / "bin" / ("height-policy-" + actor)).resolve() and
                sha256(link["binary"]) == link["sha256"], "Stale Height binary")
    return record


def finite_bound(value, bound):
    return isinstance(value, (float, int)) and not isinstance(value, bool) and math.isfinite(value) and 0 <= value < bound


def validate_rendering_proof(proof):
    """Check recorded scalar packets; rendered PNG equality is checked separately."""
    require(isinstance(proof, dict) and all(proof.get(key) is True for key in (
        "production_main_callbacks", "raw_activated_in_painting", "rendering_active_all_edits",
        "source_catalog_unchanged", "one_history_gesture", "rendered_roi_undo_exact", "rendered_roi_redo_exact")),
        "Native Rendering artist stages are incomplete")
    require(proof.get("gpu_scalar_tiles") == [1001, 1002] and proof.get("gpu_scalar_channels") == [2, 4] and
            proof.get("scalar_precision") == "L32F" and type(proof.get("cpu_interactive_composition")) is int and proof["cpu_interactive_composition"] == 0 and
            proof.get("native_program_compile_count_measured") is False and
            proof.get("native_bump_response_equivalence") is False and proof.get("performance_claim") is False and
            proof.get("hidden_normal_gpu_scope") == "tile1001 at64,64 only" and
            proof.get("hidden_normal_cpu_scope") == "center64,64 both128UDIM mirrors; constant covered3x3 H footprint", "Wrong native Rendering scope")
    captures = proof.get("captures")
    require(isinstance(captures, list) and len(captures) == 4 and all(isinstance(row, dict) for row in captures),
            "Native Rendering requires exactly four captured states")
    require([row.get("stage") for row in captures] == ["before", "edited", "undone", "redone"], "Wrong native Rendering order")
    maximum, components = 0.0, 0
    first = captures[0]
    require(isinstance(first.get("handles"), list) and first["handles"], "Missing authoritative native handles")
    history = first.get("undo_count")
    require(isinstance(history, int) and not isinstance(history, bool) and history >= 0, "Missing native history counter")
    for index, row in enumerate(captures):
        wanted = .25 if index in (0, 2) else .75
        require(row.get("expected_scalar") == wanted and row.get("fixed_bound") == 3e-5 and
                row.get("undo_count") == history + (index % 2) and row.get("handles") == first["handles"],
                "Native scalar/history/handles changed")
        render = row.get("render_state", {})
        require(isinstance(render, dict) and render.get("active") is True and render.get("image_mirror_paused") is False and
                isinstance(render.get("status"), str) and "Rendering Done" in render["status"] and
                isinstance(row.get("render_frames"), int) and not isinstance(row["render_frames"], bool) and row["render_frames"] > 0,
                "Native Rendering capture was not ready")
        native = row.get("native", {})
        parameters = native.get("parameters", []) if isinstance(native, dict) else []
        require(isinstance(parameters, list) and len(parameters) == 1 and isinstance(parameters[0], dict) and
                parameters[0].get("name") == "Scalar" and parameters[0].get("value") == [wanted] and
                native.get("height_policy") == "authored-height-raw-v1" and isinstance(native.get("channels"), list) and
                2 in native["channels"] and 4 in native["channels"] and 3 not in native["channels"], "Native Float instance differs from oracle")
        normalized = {**native, "parameters": [{**parameters[0], "value": [.25]}]}
        require(normalized == first["native"], "Native parameter edit changed source/policy/interface metadata")
        packets = row.get("samples")
        require(isinstance(packets, list) and len(packets) == 4 and all(isinstance(packet, dict) for packet in packets),
                "Missing native scalar GPU/CPU packet")
        require([(packet.get("tile"), packet.get("channel")) for packet in packets] ==
                [(1001, 2), (1001, 4), (1002, 2), (1002, 4)], "Wrong or duplicated native tile/channel")
        for packet in packets:
            expected = [wanted, wanted, wanted, 1]
            require(packet.get("xy") == [64, 64] and packet.get("expected") == expected and
                    packet.get("actual_gpu_format") == "SFLOAT_32_32_32_32", "Wrong native scalar target/oracle")
            require(all(isinstance(packet.get(key), list) and len(packet[key]) == 4 for key in ("gpu", "cpu")),
                    "Malformed native scalar component count")
            for gpu, cpu, target in zip(packet["gpu"], packet["cpu"], expected):
                require(all(isinstance(v, (float, int)) and not isinstance(v, bool) and math.isfinite(v) for v in (gpu, cpu)),
                        "Nonfinite or malformed native scalar")
                error = abs(gpu - target)
                require(gpu == cpu and finite_bound(error, 3e-5), "Native scalar GPU/CPU/oracle mismatch")
                maximum = max(maximum, error)
                components += 1
            footprint = packet.get("height_footprint_rgba")
            if packet["channel"] == 4:
                require(packet.get("height_footprint_xywh") == [63, 63, 3, 3] and isinstance(footprint, list) and len(footprint) == 9 and
                        all(isinstance(pixel, list) and len(pixel) == 4 and
                            all(isinstance(v, (int, float)) and not isinstance(v, bool) and math.isfinite(v) and finite_bound(abs(v - target), 3e-5)
                                for v, target in zip(pixel, expected)) for pixel in footprint),
                        "Hidden Normal prerequisite lacks constant fully covered 3x3 H footprint")
            else:
                require(packet.get("height_footprint_xywh") == [] and footprint == [], "Unexpected Roughness footprint claim")
        normal = row.get("hidden_normal_cpu_both_tiles")
        require(isinstance(normal, list) and len(normal) == 2 and all(isinstance(value, dict) for value in normal) and
                [value.get("tile") for value in normal] == [1001, 1002] and
                all(value.get("xy") == [64, 64] and isinstance(value.get("rgba"), list) and len(value["rgba"]) == 4 and
                    all(isinstance(v, (int, float)) and not isinstance(v, bool) and math.isfinite(v) for v in value["rgba"]) for value in normal) and
                normal == first.get("hidden_normal_cpu_both_tiles"), "Hidden Normal CPU mirrors changed")
        gpu_normal = row.get("hidden_normal_gpu_tile1001")
        require(isinstance(gpu_normal, list) and len(gpu_normal) == 4 and
                all(isinstance(v, (float, int)) and not isinstance(v, bool) and math.isfinite(v) for v in gpu_normal) and
                gpu_normal == first.get("hidden_normal_gpu_tile1001"), "Hidden Normal GPU witness changed")
        for key in ("cycles_graph_count", "cycles_svm_count"):
            value = row.get(key)
            require(isinstance(value, str) and value.isdecimal() and int(value) > 0 and value == first.get(key),
                    "Native input edit recompiled Cycles graph/SVM")
    require(captures[0]["native"] == captures[2]["native"] and captures[1]["native"] == captures[3]["native"],
            "Native Undo/Redo did not restore exact descriptors")
    return {"components": components, "maximum_scalar_error": maximum, "fixed_bound": 3e-5}


def materialize(package, output):
    data = package.read_bytes()
    require(len(data) >= 24 and data[:8] == b"MFPNAT1\n", "Expected own native v1 fixture archive")
    manifest_size, blend_size = struct.unpack(">QQ", data[8:24])
    require(manifest_size > 0 and blend_size > 0 and manifest_size <= len(data) - 24 and
            blend_size == len(data) - 24 - manifest_size, "Malformed own fixture spans")
    require(json.loads(data[24:24 + manifest_size]) == {"version": 1, "images": []},
            "Synthetic fixture must have no external-image dependencies")
    target = output / "source.blend"
    require(not target.exists(), "Preserve previous materialized fixture")
    target.write_bytes(data[24 + manifest_size:])
    return target


def validate_fixtures(path, build_sha):
    result = json.loads(path.read_text())
    require(result.get("schema") == 1 and result.get("profile") == PROFILE and
            result.get("build_sha256") == build_sha, "Fixtures belong to a different test build")
    for name in ("native", "artist"):
        group = result.get(name, {})
        require(isinstance(group, dict), "Malformed Height fixture group")
        for filename, digest in group.items():
            require(sha256(filename) == digest, "Stale Height fixture: " + filename)
    require(result.get("native"), "Missing owner fixture proof")
    return result


def run_profiles(snapshot, repo, out, env, profile, environment, confetti=None, fixtures_manifest=None):
    build_record = validate_build(snapshot, repo, out)
    build_sha = sha256(out / "build.json")
    attempt_root = out / "runs"
    attempt_root.mkdir(exist_ok=True)
    attempt = Path(tempfile.mkdtemp(prefix="attempt-", dir=attempt_root))
    shutil.copyfile(out / "build.json", attempt / "build.json")
    shutil.copyfile(out / "run-manifest.json", attempt / "run-manifest.json")
    sources = source_identity(repo)
    original = {str(environment): sha256(environment)}
    if profile in ("native", "all"):
        require(confetti is not None, "Native profile needs explicit --confetti-library")
        original[str(confetti)] = sha256(confetti)
    fixtures = validate_fixtures(fixtures_manifest, build_sha) if fixtures_manifest else {
        "schema": 1, "profile": PROFILE, "build_sha256": build_sha, "native": {}, "artist": {}}
    records = []
    bts = Path(env.get("BTS_ROOT", repo.parent)).resolve()
    actor_env = {"BTS_BLEND_MATERIAL_DEMO": "1", "BTS_BLEND_IMAGES_DEMO": "1",
                 "BTS_NATIVE_MAPPING_UPSTREAM": "1", "BTS_AE_PERSISTENCE_DEMO": "1"}

    def execute(label, actor, args, extra, marker):
        runtime = attempt / (label + "-runtime")
        runtime.mkdir()
        private_env = dict(env)
        for key in list(private_env):
            if key.startswith(("BTS_NATIVE_HEIGHT_", "BTS_BLEND_", "BTS_AE_")) or key in ("BTS_NATIVE_MATERIAL_CASES", "BTS_NATIVE_MAPPING_UPSTREAM", "BTS_HEIGHT_NEUTRAL_DEMO"):
                private_env.pop(key)
        for key, folder in (("XDG_CONFIG_HOME", "config"), ("XDG_DATA_HOME", "data"), ("XDG_CACHE_HOME", "cache"),
                            ("XDG_STATE_HOME", "state"), ("TMPDIR", "tmp"), ("BTS_RECOVERY_DIR", "recovery"),
                            ("BTS_RESOURCE_CACHE", "resources"), ("BTS_SESSION_LOG_DIR", "logs"), ("BTS_ASSET_LIBRARY_DIR", "library")):
            target = runtime / folder
            target.mkdir()
            private_env[key] = str(target)
        private_env.update(BTS_HEADLESS_WORKSPACE="1", QT_QPA_PLATFORM="offscreen", BTS_ENVIRONMENT=str(environment),
                           QT_PLUGIN_PATH=str(bts / "qt/6.8.3/plugins"),
                           LD_LIBRARY_PATH=str(bts / "qt/6.8.3/lib") + ":" + str(snapshot / "bin/lib") + ":" + env.get("LD_LIBRARY_PATH", ""), **extra)
        binary = out / "bin" / ("height-policy-" + actor)
        # Owner has a custom argv; artist runs unchanged ordinary main.
        wrapper = runtime / "host-wrapper"
        suffix = "assert sys.argv[1:3]==['--no-splash','--headless-native-height-policy-proof']\nargs=sys.argv[3:]\n" if actor == "owner" else "args=sys.argv[1:]\n"
        wrapper.write_text("#!/usr/bin/env python3\nimport os,sys\n" + suffix + "os.execv(" + repr(str(binary)) + ",[" + repr(str(binary)) + ",*args])\n")
        wrapper.chmod(0o755)
        private_env["BTS_HOST_EXECUTABLE"] = str(wrapper)
        command = ["nice", "-n", "19", "bash", str(repo / "scripts/host/run-host.sh"), *args]
        log_path = attempt / (label + ".log")
        observed_keys = {"DISPLAY", "XAUTHORITY", "WAYLAND_DISPLAY", "QT_QPA_PLATFORM", "QT_PLUGIN_PATH", "LD_LIBRARY_PATH", "TMPDIR"}
        observed_keys.update(key for key in private_env if key.startswith(("BTS_", "XDG_")))
        write_json(runtime / "launch.json", {"command": command, "binary_sha256": sha256(binary), "wrapper_sha256": sha256(wrapper),
                   "environment": {key: private_env[key] for key in sorted(observed_keys) if key in private_env}})
        try:
            with log_path.open("w") as log:
                completed = subprocess.run(command, cwd=repo, env=private_env, stdout=log, stderr=subprocess.STDOUT, timeout=360)
        except subprocess.TimeoutExpired:
            write_json(attempt / "failure.json", {"stage": label, "bounded_timeout": True, "log": str(log_path)})
            raise
        text = log_path.read_text()
        # run-host's timestamp logger can finish draining just after the native
        # child exits; this wait does not poll/render the application.
        for _ in range(100):
            if completed.returncode != 0 or "BTS_SHUTDOWN clean" in text:
                break
            time.sleep(.02)
            text = log_path.read_text()
        write_json(attempt / "progress.json", {"stage": label, "returncode": completed.returncode, "records": records,
                   "build_sha256": build_sha, "log_sha256": sha256(log_path)})
        require(completed.returncode == 0 and marker in text and "BTS_SHUTDOWN clean" in text, "Height subprocess failed: " + label)
        require(not re.search(r"OpenColorIO\s+Error|Could not find color space|Not freed memory|AddressSanitizer|Shader compilation failed|BTS_GPU_STACK_FALLBACK|BTS_.*CLEANUP_FAILED|BTS_HOST_FAILED|BTS_DRAW_FAILED|color_management\s*\|\s*ERROR", text, re.I), "Forbidden native diagnostic: " + label)
        records.append({"stage": label, "log_sha256": sha256(log_path), "binary_sha256": sha256(binary), "clean_exit": True, "empty_cache_at_launch": True, "runtime": str(runtime)})
        return text

    def checkpoint():
        write_json(attempt / "fixtures.json", fixtures)

    def group_paths(name):
        return {Path(path).name: Path(path) for path in fixtures[name]}

    if profile in ("native", "all"):
        directory = attempt / "native"
        directory.mkdir()
        execute("native", "owner", ["--no-splash", "--headless-native-height-policy-proof", str(directory)],
                {"BTS_NATIVE_HEIGHT_CONFETTI": str(confetti)}, "HEIGHT_POLICY_NATIVE PASS")
        proof = json.loads((directory / "verified.json").read_text())
        require(finite_bound(proof["affine_two_owner_two_tile"]["maximum_analytic_error"], 3e-5) and
                finite_bound(proof["actual_confetti_stack"]["maximum_native_scalar_error"], 3e-5), "Owner analytic bound failed")
        require(proof["interactive_cpu_composition"] == 0 and proof["source_none_instance_raw_coexist"] is True and
                proof["default_half_precision"]["encoded_half_error"] == 0, "Owner scope/precision contract failed")
        require(not (directory / "authored-source.blend").exists() and (directory / "authored-source.blend.held").is_file(), "Fresh child source must be absent")
        oracle = json.loads((directory / "restart-oracle.json").read_text())
        require(sha256(directory / "packed-height.blend") == oracle["project_sha256"], "Packed owner fixture drift")
        execute("native-cold", "owner", ["--no-splash", "--headless-native-height-policy-proof", str(directory), "--cold-only"], {}, "HEIGHT_POLICY_NATIVE_COLD PASS")
        cold = json.loads((directory / "cold-verified.json").read_text())["cold_open"]
        require(cold["fresh_process"] and cold["contract"] == oracle["contract"] and finite_bound(cold["maximum_gpu_error"], 1e-6), "Fresh native Open differs")
        fixtures["native"] = {str(path): sha256(path) for path in directory.iterdir() if path.is_file()}
        checkpoint()

    def setup_actor(label):
        inputs = group_paths("native")
        require({"authored-source.blend.held", "two-owner-two-tile.obj", "shared.mtl"}.issubset(inputs), "Owner fixture inputs missing")
        directory = attempt / label
        directory.mkdir()
        library = materialize(inputs["authored-source.blend.held"], directory)
        for name in ("two-owner-two-tile.obj", "shared.mtl"):
            shutil.copyfile(inputs[name], directory / name)
        project = directory / "raw-height-artist.blend"
        extra = {**actor_env, "BTS_BLEND_MATERIAL_SOURCE": str(library), "BTS_NATIVE_HEIGHT_ARTIST_PROJECT": str(project)}
        args = ["--no-splash", "--workspace", "--gui-fix-demo", str(directory), "--gui-fix-mesh", str(directory / "two-owner-two-tile.obj"), "--gui-fix-resolution", "128"]
        return directory, library, project, extra, args

    if profile == "rendering":
        require(fixtures_manifest is None, "Rendering creates its own source fixture for this exact build")
        directory = attempt / "rendering"
        directory.mkdir()
        execute("rendering-fixture", "owner", ["--no-splash", "--headless-native-height-policy-proof", str(directory),
                "--rendering-fixture-only"], {}, "HEIGHT_POLICY_RENDERING_FIXTURE PASS")
        fixture = json.loads((directory / "rendering-fixture.json").read_text())
        package = directory / "rendering-source.btsnative"
        require(fixture.get("material") == "Native Rendering scalar fixture" and fixture.get("parameter") == "Scalar" and
                fixture.get("value") == .25 and fixture.get("shared_float_consumers") == [2, 4] and
                fixture.get("source_policy") == "none" and fixture.get("default_bump_available") is False and
                fixture.get("gpu_context_created") is False and fixture.get("package_sha256") == sha256(package),
                "Wrong own native Rendering source fixture")
        library = materialize(package, directory)
        mesh, material = repo / "fixtures/two_tile_udim.obj", repo / "fixtures/two_tile_udim.mtl"
        inputs = (package, library, mesh, material)
        before = {str(path): sha256(path) for path in inputs}
        original.update(before)
        execute("rendering", "artist", ["--no-splash", "--workspace", "--gui-fix-demo", str(directory),
                "--gui-fix-mesh", str(mesh), "--gui-fix-resolution", "128"],
                {**actor_env, "BTS_NATIVE_HEIGHT_RENDERING": "1", "BTS_BLEND_MATERIAL_SOURCE": str(library)},
                "HEIGHT_POLICY_NATIVE_RENDERING PASS")
        require(before == {path: sha256(path) for path in before}, "Rendering changed original source/mesh bytes")
        proof = json.loads((directory / "native-rendering-verified.json").read_text())
        metrics = validate_rendering_proof(proof)
        roi = {name: sha256(directory / ("native-rendering-" + name + "-roi.png"))
               for name in ("before", "edited", "undone", "redone")}
        require(roi["before"] == roi["undone"] and roi["edited"] == roi["redone"] and roi["before"] != roi["edited"],
                "Native Rendering shaded ROI did not change/restore exactly")
        write_json(directory / "rendering-result.json", {**metrics, "roi_sha256": roi, "source_sha256_before_after": before,
                   "proof_sha256": sha256(directory / "native-rendering-verified.json"), "performance_claim": False})
        fixtures["rendering"] = {str(path): sha256(path) for path in directory.iterdir() if path.is_file()}
        checkpoint()

    if profile in ("artist", "all"):
        directory, library, project, extra, args = setup_actor("artist")
        library_sha = sha256(library)
        execute("artist", "artist", args, extra, "HEIGHT_POLICY_ARTIST PASS")
        artist = json.loads((directory / "artist-verified.json").read_text())
        require(artist["production_main_callbacks"] is True and artist["fresh_open"] is False and
                artist["cpu_interactive_composition"] == 0 and
                all(artist[key] is True for key in ("actual_selector_cancel_reset_undo_redo", "default_policy_off",
                    "cpu_cancel_exact_old_state", "owner_stage_cancel_exact_old_state", "policy_reset_undoable", "one_history_gesture_per_parameter")),
                "Artist sequence did not complete every required stage")
        require(sha256(library) == library_sha and project.is_file(), "Artist fixture drift or Save absent")
        library.rename(library.with_suffix(".blend.held"))
        cold_dir = attempt / "artist-cold"
        cold_dir.mkdir()
        descriptor = directory / "artist-descriptor.json"
        before = {str(path): sha256(path) for path in (project, descriptor, library.with_suffix(".blend.held"))}
        execute("artist-cold", "artist", ["--no-splash", "--workspace", "--gui-fix-demo", str(cold_dir), "--gui-fix-resolution", "128"],
                {**actor_env, "BTS_NATIVE_HEIGHT_ARTIST_REOPEN": "1", "BTS_NATIVE_HEIGHT_ARTIST_PROJECT": str(project), "BTS_NATIVE_HEIGHT_ARTIST_DESCRIPTOR": str(descriptor)}, "HEIGHT_POLICY_ARTIST PASS")
        require(not library.exists() and before == {path: sha256(path) for path in before}, "Cold artist changed packed/hidden inputs")
        proof = json.loads((cold_dir / "artist-verified.json").read_text())
        require(proof["fresh_open"] and len(proof["captures"]) == 1, "Missing fresh artist Open witness")
        fixtures["artist"] = before
        checkpoint()

    if profile in ("lifecycle", "all"):
        for mode in ("cpu", "gpu"):
            directory, library, project, extra, args = setup_actor("quit-" + mode)
            before = sha256(library)
            execute("quit-" + mode, "artist", args, {**extra, "BTS_NATIVE_HEIGHT_ACTUAL_QUIT": mode}, "HEIGHT_POLICY_ACTUAL_QUIT")
            proof = json.loads((directory / "quit-request.json").read_text())
            require(proof["phase"] == ("worker-result-not-delivered" if mode == "cpu" else "owner-gpu-staged-before-publication") and
                    all(proof[key] is True for key in ("real_application_quit_requested", "selector_disabled", "real_cancel_visible", "instance_still_none", "admission_unchanged")), "Actual quit contract failed")
            require(not project.exists() and sha256(library) == before, "Pending quit published or changed source")
        artist_inputs = group_paths("artist")
        require("raw-height-artist.blend" in artist_inputs, "Startup explicit-project needs artist fixtures")
        for mode in ("bare", "explicit-project"):
            directory = attempt / ("startup-" + mode)
            directory.mkdir()
            extra = {"BTS_NATIVE_HEIGHT_BARE_SMOKE": "1", "BTS_NATIVE_HEIGHT_BARE_OUTPUT": str(directory)}
            args = ["--no-splash"]
            if mode == "explicit-project":
                args += ["--workspace", "--project", str(artist_inputs["raw-height-artist.blend"])]
                extra["BTS_NATIVE_HEIGHT_STARTUP_EXPECT_WORKSPACE"] = "1"
            text = execute("startup-" + mode, "artist", args, extra, "HEIGHT_POLICY_BARE PASS")
            require("HEIGHT_POLICY_ARTIST_STAGE" not in text and "BTS_NEW_PROJECT" not in text,
                    "Startup smoke unexpectedly ran an artist/New sequence")
            proof = json.loads((directory / "bare-verified.json").read_text())
            expected = "ACEScg" if mode == "explicit-project" else "Linear Rec.709"
            context = proof["read_only_current_context"]
            require(proof["workspace"] == (mode == "explicit-project") and not proof["new_project_requested"] and
                    context["recorded_name"] == context["active_name"] == expected and
                    context["recorded_scene_linear_to_xyz"] == context["active_scene_linear_to_xyz"] and
                    len(context["active_scene_linear_to_xyz"]) == 9 and
                    all(isinstance(v, (int, float)) and math.isfinite(v) for v in context["active_scene_linear_to_xyz"]) and
                    context["context_modified"] is False and proof["private_gpu_framebuffer_ready"] is True and
                    proof["headless_gpu_ready_property"] is True and proof["width"] > 0 and proof["height"] > 0 and
                    proof["diagnostic_framebuffer_readbacks"] == 1 and not proof["desktop_grab_or_input"] and
                    not proof["performance_claim"], "Startup mode/context changed")
    require(source_identity(repo) == sources and original == {path: sha256(path) for path in original}, "Original input/runner changed")
    validate_build(snapshot, repo, out)
    checkpoint()
    write_json(attempt / "verified.json", {"schema": 1, "profile": profile, "build_sha256": build_sha,
               "records": records, "original_sha256_before_after": original, "performance_claim": False})
    return attempt
