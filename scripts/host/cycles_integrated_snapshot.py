"""Admission of a fully rebuilt, independently frozen native/fork integration.

This never applies patches or compiles production translation units. The build
confirmation is an observed command result supplied by the integration runner;
the separate Ninja dry-run admits only the existing source guard and the exact
pinned buildinfo timestamp generator/object/link chain. No other production
compilation, archive or regeneration is accepted.
"""
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import sys

from cycles_live_manifest import sha256 as _sha256

PIN = "fbe6228777e7d9afefcd61a413844e790ae75db7"
SOURCE_SUFFIXES = {".cc", ".cpp", ".c", ".hh", ".h", ".hpp", ".inc", ".inl"}
CONTROLLED_PROFILE = "controlled-fixed-sampling-v1"
CONTROLLED_TEST_SOURCES = {
    "native/tests/cycles_live_udim_artist_main.cc", "native/tests/cycles_live_udim_artist_test.cc",
    "native/tests/CyclesLiveUdimArtist.hh", "native/tests/CyclesLiveCompletionProbe.hh",
    "native/tests/CyclesLiveCompletionProbe.cc",
}
DISPLACEMENT_PROFILE = "live-displacement-controlled-v1"
DISPLACEMENT_TEST_SOURCES = {
    "native/tests/CyclesLiveDisplacementProbe.hh", "native/tests/CyclesLiveDisplacementProbe.cc",
    "native/tests/CyclesLiveDisplacementArtist.hh", "native/tests/cycles_live_displacement_main.cc",
    "native/tests/cycles_live_displacement_artist_test.cc",
}
_verified_digests = {}


def sha256(path):
    """Read bytes once per unchanged POSIX identity; never trust mtime alone."""
    path = Path(path).resolve()
    if os.name != "posix":
        return _sha256(path)
    def identity():
        stat = path.stat()
        return stat.st_dev, stat.st_ino, stat.st_size, stat.st_mtime_ns, stat.st_ctime_ns
    before = identity()
    known = _verified_digests.get(path)
    if known is not None and known[0] == before:
        return known[1]
    digest = _sha256(path)
    if identity() != before:
        raise ValueError("Input changed while hashing: " + str(path))
    _verified_digests[path] = before, digest
    return digest


def is_link_input(value):
    return not value.startswith("-") and (value.endswith((".o", ".a", ".dylib", ".lib")) or
                                           re.search(r"\.so(?:\.[0-9]+)*$", value) is not None)


def dependency_path(name, build):
    # Keep the lexical include path: Blender's lib/ can be a symlink to the
    # external SDK. The relocated compiler still needs that exact directory
    # layout, with the referenced headers copied into the private tree.
    path = Path(name)
    return Path(os.path.abspath(path if path.is_absolute() else build / path))


def link_arguments(commands):
    args = shlex.split(commands[-1])
    if args[:2] == [":", "&&"]:
        args = args[2:]
    return args[:args.index("&&")] if "&&" in args else args


def native_units(commands):
    units = {}
    for command in commands:
        args = shlex.split(command)
        if "-c" not in args or "-o" not in args:
            continue
        source = args[args.index("-c") + 1]
        if "/native/" in source:
            units[args[args.index("-o") + 1]] = source
    if not any(source.endswith("/NativeCyclesImages.cc") for source in units.values()):
        raise ValueError("Snapshot lacks the production NativeCyclesImages compile unit")
    return units


def native_dependencies(text, units):
    deps, key = {}, None
    for line in text.splitlines():
        if ": #deps " in line:
            key = line.split(": #deps ", 1)[0]
            if "(VALID)" not in line or key in deps:
                raise ValueError("Invalid or duplicated native dependency entry")
            deps[key] = []
        elif key is not None and line.startswith("    "):
            deps[key].append(line.strip())
    if set(deps) != set(units) or any(not values for values in deps.values()):
        raise ValueError("Incomplete frozen native dependency closure")
    return deps


def buildinfo_commands(commands, blender):
    """The pinned timestamp generator and its sole metadata object/link edge."""
    admitted = {}
    for command in commands:
        args = shlex.split(command)
        body = args[3:] if args[:1] == ["cd"] and args[2:3] == ["&&"] else args
        if ("-P" in body and body[-2:] == ["-P", str(blender / "build_files/cmake/buildinfo.cmake")] and
                Path(body[0]).name == "cmake"):
            admitted[tuple(args)] = "buildinfo-generator"
        elif ("-c" in args and "-o" in args and args[args.index("-c") + 1] == str(blender / "source/creator/buildinfo.c") and
              args[args.index("-o") + 1].endswith("/buildinfo.c.o")):
            admitted[tuple(args)] = "buildinfo-object"
    if set(admitted.values()) == {"buildinfo-generator", "buildinfo-object"}:
        admitted[tuple(shlex.split(commands[-1]))] = "buildinfo-link"
    elif admitted:
        raise ValueError("Incomplete pinned buildinfo metadata edge")
    return admitted


def admitted_dry_run(text, repo, build, commands=(), blender=None):
    """Allow the source guard and exact pinned timestamp-only metadata chain."""
    admitted = []
    guard = (repo / "scripts/host/verify-gpu-only-paths.py").resolve()
    metadata = buildinfo_commands(commands, blender) if commands and blender else {}
    seen_metadata = []
    for line in text.splitlines():
        if not line or line.startswith("ninja: Entering directory") or line == "ninja: no work to do.":
            continue
        match = re.fullmatch(r"\[\d+/\d+\] (.+)", line)
        if not match:
            raise ValueError("Unknown Ninja dry-run output: " + line)
        args = shlex.split(match[1])
        if tuple(args) in metadata:
            kind = metadata[tuple(args)]
            seen_metadata.append(kind)
            admitted.append({"kind": kind, "command": match[1], "sha256": hashlib.sha256(match[1].encode()).hexdigest()})
            continue
        if len(args) >= 3 and args[0] == "cd" and args[2] == "&&":
            if not Path(args[1]).resolve().is_relative_to(build.resolve()):
                raise ValueError("Source guard has an unexpected working directory")
            args = args[3:]
        if len(args) != 2 or not Path(args[0]).name.startswith("python3") or Path(args[1]).resolve() != guard:
            raise ValueError("Pending production build operation: " + match[1])
        admitted.append({"kind": "gpu-source-guard", "command": match[1], "sha256": hashlib.sha256(match[1].encode()).hexdigest()})
    if seen_metadata and sorted(seen_metadata) != ["buildinfo-generator", "buildinfo-link", "buildinfo-object"]:
        raise ValueError("Partial or duplicated buildinfo metadata-only chain")
    return admitted


def patch_series(repo):
    paths = sorted((repo / "native/patches").glob("*.patch"))
    owned_self_patch = "0040-partial-write-owned-embedded-self-cache.patch"
    hosted_workers_patch = "0041-hosted-shader-worker-contexts.patch"
    hosted_programs_patch = "0042-hosted-stack-program-warmup.patch"
    if (not paths or paths[-1].name not in ("0036-cycles-live-image-reload.patch", "0037-cycles-hosted-displacement-image-refresh.patch", "0038-hosted-immutable-source-snapshots.patch", owned_self_patch, hosted_workers_patch, hosted_programs_patch) or
            not {"0035-hosted-live-image-cache.patch", "0036-cycles-live-image-reload.patch"}.issubset(path.name for path in paths)):
        raise ValueError("Unexpected reviewed fork patch-series boundary")
    if any(path.name.startswith("0037-") and path.name != "0037-cycles-hosted-displacement-image-refresh.patch" for path in paths):
        raise ValueError("Unexpected reviewed displacement fork patch")
    if any(path.name.startswith("0038-") and path.name != "0038-hosted-immutable-source-snapshots.patch" for path in paths):
        raise ValueError("Unexpected reviewed immutable-source fork patch")
    if paths[-1].name == "0038-hosted-immutable-source-snapshots.patch" and not any(path.name == "0037-cycles-hosted-displacement-image-refresh.patch" for path in paths):
        raise ValueError("Immutable-source extension requires the reviewed displacement predecessor")
    if any(path.name > "0038-hosted-immutable-source-snapshots.patch" and path.name not in (owned_self_patch, hosted_workers_patch, hosted_programs_patch) for path in paths):
        raise ValueError("Unexpected reviewed persistence fork patch")
    if paths[-1].name == owned_self_patch and not {"0037-cycles-hosted-displacement-image-refresh.patch", "0038-hosted-immutable-source-snapshots.patch"}.issubset(path.name for path in paths):
        raise ValueError("Owned embedded self-cache extension requires the reviewed source predecessors")
    if paths[-1].name == hosted_workers_patch and not {"0037-cycles-hosted-displacement-image-refresh.patch", "0038-hosted-immutable-source-snapshots.patch", owned_self_patch}.issubset(path.name for path in paths):
        raise ValueError("Hosted shader workers require the reviewed source/persistence predecessors")
    if paths[-1].name == hosted_programs_patch and not {"0037-cycles-hosted-displacement-image-refresh.patch", "0038-hosted-immutable-source-snapshots.patch", owned_self_patch, hosted_workers_patch}.issubset(path.name for path in paths):
        raise ValueError("Hosted stack programs require the reviewed source/persistence/worker predecessors")
    return {str(path.relative_to(repo)): sha256(path) for path in paths}


def _record_copy(source, destination):
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(source, destination)
    return sha256(destination)


def capture(snapshot, repo, blender, build_result, cpu_results, fork_receipt, original_build_log=None):
    snapshot, repo, blender = map(lambda p: Path(p).resolve(), (snapshot, repo, blender))
    target = snapshot / "integrated-snapshot.json"
    if target.exists():
        raise ValueError("Preserve the existing integrated snapshot attestation")
    meta = json.loads((snapshot / "snapshot.json").read_text())
    build = Path(meta["source_build"]).resolve()
    if snapshot.is_relative_to(build) or build.is_relative_to(snapshot):
        raise ValueError("The frozen snapshot must be separate from the artist build")
    result = json.loads(Path(build_result).read_text())
    if result.get("returncode") != 0 or result.get("source_head") != meta["source_head"] or not {"bts-host", "bts-cpu-tests"}.issubset(result.get("command", [])):
        raise ValueError("Missing successful completed-build confirmation for this snapshot")
    checks = json.loads(Path(cpu_results).read_text())
    if not isinstance(checks, list) or not checks or any(check.get("returncode") != 0 for check in checks):
        raise ValueError("The complete CPU result must contain only successful checks")
    pin = subprocess.check_output(["git", "-C", str(blender), "rev-parse", "HEAD"], text=True).strip()
    if pin != PIN:
        raise ValueError("Unexpected pinned Blender revision")
    series = patch_series(repo)
    verified = json.loads(Path(fork_receipt).read_text())
    if (verified.get("schema") != 1 or verified.get("pin") != PIN or
            verified.get("source_head") != meta["source_head"] or
            verified.get("exact_reviewed_series_equal") is not True or
            verified.get("read_only_actual_checkout") is not True or
            verified.get("patch_sha256") != series or not verified.get("actual_fork_sha256")):
        raise ValueError("Missing exact reviewed fork-series verification receipt")
    for relative, expected in verified["actual_fork_sha256"].items():
        if sha256(blender / relative) != expected:
            raise ValueError("Fork changed since exact-series verification: " + relative)
    modified = set(subprocess.check_output(["git", "-C", str(blender), "diff", "--name-only", PIN], text=True).splitlines())
    modified.update(subprocess.check_output(["git", "-C", str(blender), "ls-files", "--others", "--exclude-standard"], text=True).splitlines())
    if not modified.issubset(verified["actual_fork_sha256"]):
        raise ValueError("Fork has changes outside the reviewed patch series")
    fork_paths = set()
    for relative in series:
        for line in (repo / relative).read_text().splitlines():
            if line.startswith(("--- a/", "+++ b/")):
                fork_paths.add(line[6:].split("\t", 1)[0])
    if not fork_paths.issubset(verified["actual_fork_sha256"]):
        raise ValueError("Incomplete exact-series fork file receipt")
    commands = json.loads((snapshot / "host-commands.json").read_text())
    metadata = buildinfo_commands(commands, blender)
    if metadata:
        fork_paths.update(("build_files/cmake/buildinfo.cmake", "source/creator/buildinfo.c"))
    linker_scripts = {}
    for argument in link_arguments(commands):
        if argument.startswith("-Wl,--version-script="):
            source = Path(argument.split("=", 1)[1]).resolve()
            if not source.is_relative_to(blender):
                raise ValueError("Unexpected production linker script")
            relative = str(source.relative_to(blender))
            fork_paths.add(relative)
            linker_scripts[argument] = relative
    units = native_units(commands)
    deps = native_dependencies((snapshot / "native-deps.txt").read_text(), units)
    evidence = snapshot / "integration-evidence"
    evidence.mkdir(exist_ok=True)
    dry = subprocess.run(["nice", "-n", "19", "ninja-build", "-C", str(build), "-n", "-v", "bts-host", "bts-cpu-tests"], capture_output=True, text=True, check=True)
    allowed = admitted_dry_run(dry.stdout, repo, build, commands, blender)
    (evidence / "ninja-dry-run.log").write_text(dry.stdout + dry.stderr)
    with (evidence / "gpu-source-guard.log").open("w") as log:
        subprocess.run(["nice", "-n", "19", sys.executable, str(repo / "scripts/host/verify-gpu-only-paths.py")], cwd=repo, stdout=log, stderr=subprocess.STDOUT, check=True)
    receipts = {}
    for label, source in (("completed-build-result.json", build_result), ("completed-build.log", result["log"]), ("cpu-results.json", cpu_results), ("fork-series-receipt.json", fork_receipt)):
        receipts["integration-evidence/" + label] = _record_copy(Path(source), evidence / label)
    if original_build_log:
        receipts["integration-evidence/original-install.log"] = _record_copy(Path(original_build_log), evidence / "original-install.log")
    for label in ("ninja-dry-run.log", "gpu-source-guard.log"):
        receipts["integration-evidence/" + label] = sha256(evidence / label)
    native = {}
    for source in (repo / "native").rglob("*"):
        if source.is_file() and source.suffix in SOURCE_SUFFIXES:
            relative = str(source.relative_to(repo))
            native[relative] = _record_copy(source, snapshot / "integration-source" / relative)
    fork, generated = {}, {}
    for names in deps.values():
        for name in names:
            source = dependency_path(name, build)
            if source.is_relative_to(blender):
                fork_paths.add(str(source.relative_to(blender)))
            elif source.is_relative_to(build):
                relative = str(source.relative_to(build))
                if relative not in generated:
                    generated[relative] = _record_copy(source, snapshot / "integration-generated" / relative)
    for relative in sorted(fork_paths):
        fork[relative] = _record_copy(blender / relative, snapshot / "integration-blender" / relative)
    inputs = {}
    for token in link_arguments(commands):
        if not is_link_input(token) or token in inputs:
            continue
        source = Path(token)
        source = (source if source.is_absolute() else build / source).resolve()
        if source.is_relative_to(build):
            relative = str(source.relative_to(build))
            frozen = (snapshot / relative).resolve()
            if not frozen.is_relative_to(snapshot) or not frozen.is_file():
                raise ValueError("Missing or escaping frozen link input: " + token)
            if os.path.samestat(source.stat(), frozen.stat()) or sha256(source) != sha256(frozen):
                raise ValueError("Frozen link input is shared or differs from completed build: " + token)
            inputs[token] = {"kind": "frozen", "path": relative, "sha256": sha256(frozen)}
        else:
            inputs[token] = {"kind": "sdk-absolute", "path": str(source), "sha256": sha256(source)}
    manifest = {"schema": 1, "kind": "full-native-fork-integration", "native_source_head": meta["source_head"], "production_repo": str(repo),
                "source_build": str(build), "blender_root": str(blender), "blender_pin": pin,
                "fork_modified_paths": sorted(modified), "gpu_source_guard_sha256": sha256(repo / "scripts/host/verify-gpu-only-paths.py"),
                "patch_series_sha256": series, "native_source_sha256": native, "blender_source_sha256": fork,
                "generated_sha256": generated, "link_inputs": inputs, "evidence_sha256": receipts,
                "linker_scripts": linker_scripts,
                "commands_sha256": sha256(snapshot / "host-commands.json"), "dependencies_sha256": sha256(snapshot / "native-deps.txt"),
                "snapshot_metadata_sha256": sha256(snapshot / "snapshot.json"), "native_compile_units": len(units),
                "pending_commands": allowed, "closure_evidence": "Completed full-target build confirmation; dry run allows only exact source guard and pinned buildinfo timestamp generator/object/relink chain; no other pending compile/link/archive/install; every native Ninja dependency entry VALID",
                "original_install_exit_observed": False}
    target.write_text(json.dumps(manifest, indent=2) + "\n")
    validate(snapshot, repo)
    return target



def require_displacement_options(options):
    """The one128 displacement actor is independent of all older profiles."""
    if not options.displacement_profile:
        return
    if (not options.integrated_snapshot or options.udim or options.cpu_only or
            options.sanitize_bridge or options.controlled_profile or options.burst_profile or
            options.test_parent_manifest or options.controlled_parent_manifest or
            options.burst_previous_attempt or (options.build_only and options.run_only)):
        raise ValueError("Displacement requires only an integrated snapshot and its own 128 fixture")


def validate(snapshot, repo, test_overlay=None, profile=None):
    snapshot, repo = Path(snapshot).resolve(), Path(repo).resolve()
    path = snapshot / "integrated-snapshot.json"
    m = json.loads(path.read_text())
    overlay = test_overlay or {}
    if profile == DISPLACEMENT_PROFILE and set(overlay) != DISPLACEMENT_TEST_SOURCES:
        raise ValueError("Displacement requires exactly its five test-only source records")
    if overlay:
        if profile == "held-picker-burst-6s-v1":
            from cycles_burst_snapshot import validate_overlay
            validate_overlay(snapshot, repo, overlay)
        elif profile == DISPLACEMENT_PROFILE:
            pass  # Exact displacement overlay checked above, even when empty.
        elif set(overlay) != CONTROLLED_TEST_SOURCES or profile not in (None, CONTROLLED_PROFILE):
            raise ValueError("Undeclared test source overlay/profile")
        for relative, info in overlay.items():
            if (info.get("original_sha256") != m.get("native_source_sha256", {}).get(relative) or
                    sha256(repo / relative) != info.get("sha256")):
                raise ValueError("Stale controlled test source overlay: " + relative)
    if m.get("schema") != 1 or m.get("kind") != "full-native-fork-integration" or m.get("blender_pin") != PIN:
        raise ValueError("Not an admitted full native/fork integration snapshot")
    if m.get("patch_series_sha256") != patch_series(repo):
        raise ValueError("Stale or incomplete reviewed fork patch series")
    for name, field in (("snapshot.json", "snapshot_metadata_sha256"), ("host-commands.json", "commands_sha256"), ("native-deps.txt", "dependencies_sha256")):
        if sha256(snapshot / name) != m.get(field):
            raise ValueError("Stale integrated build metadata: " + name)
    meta = json.loads((snapshot / "snapshot.json").read_text())
    if meta["source_head"] != m.get("native_source_head"):
        raise ValueError("Integrated snapshot source identity differs")
    commands = json.loads((snapshot / "host-commands.json").read_text())
    units = native_units(commands)
    deps = native_dependencies((snapshot / "native-deps.txt").read_text(), units)
    if len(units) != m.get("native_compile_units"):
        raise ValueError("Integrated native compile-unit count differs")
    required = {"integration-evidence/" + name for name in ("completed-build-result.json", "completed-build.log", "cpu-results.json", "ninja-dry-run.log", "gpu-source-guard.log", "fork-series-receipt.json")}
    if not required.issubset(m.get("evidence_sha256", {})):
        raise ValueError("Missing completed-build/CPU/header-closure evidence")
    for relative, expected in m["evidence_sha256"].items():
        if sha256(snapshot / relative) != expected:
            raise ValueError("Stale integrated build evidence: " + relative)
    result = json.loads((snapshot / "integration-evidence/completed-build-result.json").read_text())
    checks = json.loads((snapshot / "integration-evidence/cpu-results.json").read_text())
    if result.get("returncode") != 0 or result.get("source_head") != m["native_source_head"] or not {"bts-host", "bts-cpu-tests"}.issubset(result.get("command", [])) or not checks or any(check.get("returncode") != 0 for check in checks):
        raise ValueError("Unsuccessful integrated build/CPU confirmation")
    allowed = admitted_dry_run((snapshot / "integration-evidence/ninja-dry-run.log").read_text(), Path(m["production_repo"]), Path(m["source_build"]), commands, Path(m["blender_root"]))
    if allowed != m.get("pending_commands"):
        raise ValueError("Changed integrated Ninja pending-operation evidence")
    verified = json.loads((snapshot / "integration-evidence/fork-series-receipt.json").read_text())
    if (verified.get("pin") != PIN or verified.get("source_head") != m["native_source_head"] or
            verified.get("patch_sha256") != m["patch_series_sha256"] or
            verified.get("exact_reviewed_series_equal") is not True or
            verified.get("read_only_actual_checkout") is not True):
        raise ValueError("Incorrect integrated reviewed-series receipt")
    patch_files = set()
    for relative in m["patch_series_sha256"]:
        patch_files.update(line[6:].split("\t", 1)[0] for line in (repo / relative).read_text().splitlines()
                           if line.startswith(("--- a/", "+++ b/")))
    if not patch_files.issubset(verified.get("actual_fork_sha256", {})):
        raise ValueError("Incomplete integrated reviewed-series file receipt")
    if not set(m.get("fork_modified_paths", [])).issubset(verified["actual_fork_sha256"]):
        raise ValueError("Unreviewed integrated fork changes")
    if sha256(repo / "scripts/host/verify-gpu-only-paths.py") != m.get("gpu_source_guard_sha256"):
        raise ValueError("Integrated GPU source guard changed")
    for relative, expected in verified["actual_fork_sha256"].items():
        if m.get("blender_source_sha256", {}).get(relative) != expected:
            raise ValueError("Frozen fork differs from exact reviewed series: " + relative)
    expected_scripts = {value: str(Path(value.split("=", 1)[1]).relative_to(Path(m["blender_root"])))
                        for value in link_arguments(commands) if value.startswith("-Wl,--version-script=")}
    if m.get("linker_scripts", {}) != expected_scripts or any(relative not in m["blender_source_sha256"] for relative in expected_scripts.values()):
        raise ValueError("Missing exact frozen production linker script")
    if buildinfo_commands(commands, Path(m["blender_root"])) and not {"build_files/cmake/buildinfo.cmake", "source/creator/buildinfo.c"}.issubset(m["blender_source_sha256"]):
        raise ValueError("Missing pinned buildinfo metadata source identity")
    for source in units.values():
        relative = "native/" + source.split("/native/", 1)[1]
        if relative not in m.get("native_source_sha256", {}):
            raise ValueError("Missing compiled native source identity: " + relative)
    current_native = {str(path.relative_to(repo)) for path in (repo / "native").rglob("*")
                      if path.is_file() and path.suffix in SOURCE_SUFFIXES}
    if set(m.get("native_source_sha256", {})) | set(overlay) != current_native:
        raise ValueError("Incomplete integrated native source/header set")
    build_root, fork_root = Path(m["source_build"]).resolve(), Path(m["blender_root"]).resolve()
    for names in deps.values():
        for name in names:
            dep = dependency_path(name, build_root)
            if any(str(dep).endswith("/" + relative) for relative in overlay):
                raise ValueError("Controlled test override is a production dependency: " + str(dep))
            if dep.is_relative_to(fork_root):
                if str(dep.relative_to(fork_root)) not in m.get("blender_source_sha256", {}):
                    raise ValueError("Missing frozen Blender header dependency: " + name)
            elif dep.is_relative_to(build_root):
                if str(dep.relative_to(build_root)) not in m.get("generated_sha256", {}):
                    raise ValueError("Missing frozen generated header dependency: " + name)
            elif any(str(dep).startswith(source.split("/native/", 1)[0] + "/native/") for source in units.values()):
                if "native/" + str(dep).split("/native/", 1)[1] not in m["native_source_sha256"]:
                    raise ValueError("Missing frozen native header dependency: " + name)
    source_sets = (("native_source_sha256", "integration-source"), ("blender_source_sha256", "integration-blender"), ("generated_sha256", "integration-generated"))
    for field, folder in source_sets:
        if not m.get(field):
            raise ValueError("Missing integrated source/header identity: " + field)
        for relative, expected in m[field].items():
            frozen = (snapshot / folder / relative).resolve()
            if not frozen.is_relative_to(snapshot) or sha256(frozen) != expected:
                raise ValueError("Stale frozen source/header: " + relative)
            if field == "native_source_sha256" and relative not in overlay and sha256(repo / relative) != expected:
                raise ValueError("Current native source differs from integrated production: " + relative)
    expected_tokens = {v for v in link_arguments(commands) if is_link_input(v)}
    if set(m.get("link_inputs", {})) != expected_tokens:
        raise ValueError("Incomplete integrated production link inputs")
    for token, info in m["link_inputs"].items():
        original = Path(token)
        original = (original if original.is_absolute() else build_root / original).resolve()
        if info.get("kind") == "frozen":
            source = (snapshot / info["path"]).resolve()
            if (not source.is_relative_to(snapshot) or not original.is_relative_to(build_root) or
                    str(original.relative_to(build_root)) != info["path"]):
                raise ValueError("Integrated input escapes immutable snapshot")
        elif info.get("kind") == "sdk-absolute" and Path(info["path"]).is_absolute():
            source = Path(info["path"])
            if source.resolve().is_relative_to(build_root) or source.resolve() != original:
                raise ValueError("Integrated SDK input points into mutable artist build")
        else:
            raise ValueError("Unknown integrated input provenance")
        if sha256(source) != info.get("sha256"):
            raise ValueError("Stale integrated production/SDK input: " + token)
    return m


def runner_identities(repo, burst=False, displacement=False):
    names = ("cycles_integrated_snapshot.py", "cycles_live_manifest.py", "verify-cycles-live.py", "capture-cycles-snapshot.py")
    if burst:
        names += ("cycles_burst_snapshot.py", "check-cycles-live-burst.py")
    identities = {"scripts/host/" + name: sha256(repo / "scripts/host" / name) for name in names}
    if burst:
        identities["native/tests/cycles_live_burst_plan.md"] = sha256(repo / "native/tests/cycles_live_burst_plan.md")
    if displacement:
        for relative in ("native/tests/cycles_live_displacement_plan.md", "native/tests/cycles_displacement_profile_test.py"):
            identities[relative] = sha256(repo / relative)
    return identities


def validate_runner_sources(repo, recorded, burst=False, displacement=False):
    if recorded != runner_identities(repo, burst, displacement):
        raise ValueError("Runner source changed since burst/build admission")


def make_dependencies(path):
    """Read GCC -M output, retaining escaped spaces in actual header paths."""
    text = path.read_text().replace("\\\n", "")
    return [Path(value) for value in shlex.split(text.split(":", 1)[1])]


def pinned_test_header(fork, relative, gitlinks):
    """Resolve tracked fork headers, including SDK headers in a pinned gitlink."""
    parts = Path(relative).parts
    if len(parts) > 2 and parts[0] == "lib":
        submodule = str(Path(*parts[:2]))
        if submodule not in gitlinks:
            entry = subprocess.check_output(["git", "-C", str(fork), "ls-tree", PIN, "--", submodule], text=True).strip().split()
            if len(entry) != 4 or entry[:2] != ["160000", "commit"] or entry[3] != submodule or not re.fullmatch(r"[0-9a-f]{40}", entry[2]):
                raise ValueError("SDK test header has no exact pinned gitlink: " + relative)
            actual = subprocess.check_output(["git", "-C", str(fork / submodule), "rev-parse", "HEAD"], text=True).strip()
            dirty = subprocess.check_output(["git", "-C", str(fork / submodule), "status", "--porcelain", "--untracked-files=no"], text=True)
            if actual != entry[2] or dirty:
                raise ValueError("SDK test header checkout is not the clean pinned gitlink: " + relative)
            gitlinks[submodule] = entry[2]
        pin = gitlinks[submodule]
        path = str(Path(*parts[2:]))
        blob = subprocess.check_output(["git", "-C", str(fork / submodule), "rev-parse", pin + ":" + path], text=True).strip()
        if not re.fullmatch(r"[0-9a-f]{40}", blob):
            raise ValueError("Invalid pinned SDK header blob: " + relative)
        content = subprocess.check_output(["git", "-C", str(fork / submodule), "show", pin + ":" + path])
        if content.startswith(b"version https://git-lfs.github.com/spec/v1"):
            raise ValueError("Expanded LFS SDK test header is unsupported: " + relative)
        return content, {"kind": "pinned-gitlink", "pin": PIN, "submodule": submodule, "submodule_pin": pin, "submodule_path": path, "blob": blob}
    return subprocess.check_output(["git", "-C", str(fork), "show", PIN + ":" + relative]), {"kind": "pinned-git-show", "pin": PIN}


def freeze_test_headers(snapshot, out, admitted, commands, call, native_source=None, native_sha256=None):
    """Discover all test-only dependencies before compiling any translation unit.

    Discovery may read the original reviewed fork. Final compilation only sees
    frozen production headers plus this separately attested test supplement.
    Unmodified fork extras must equal git-show at the pinned revision; external
    SDK dependencies retain their exact absolute paths and byte identities.
    """
    fork = Path(admitted["blender_root"])
    build = Path(admitted["source_build"])
    frozen = native_source or snapshot / "integration-source"
    native_sha256 = native_sha256 or admitted["native_source_sha256"]
    supplement = out / "test-headers/blender"
    # A newly needed header can include an already admitted header by a
    # relative path. Preserve that topology in one private frozen tree;
    # separate include search prefixes cannot repair relative includes.
    production_headers = admitted["blender_source_sha256"]
    for relative, digest in production_headers.items():
        original, target = snapshot / "integration-blender" / relative, supplement / relative
        if sha256(original) != digest:
            raise ValueError("Stale production header while freezing test context: " + relative)
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(original, target)
        if sha256(target) != digest:
            raise ValueError("Stale copied production test header: " + relative)
    records, external, closures, scans, gitlinks = {}, {}, {}, {}, {}
    for name, args in commands.items():
        dep = out / (Path(name).stem + ".discovery.d")
        scan = list(args)
        for flag in ("-o", "-MF", "-MT"):
            if flag in scan:
                index = scan.index(flag)
                del scan[index:index + 2]
        scan = [value for value in scan if value not in ("-c", "-MD", "-MMD", "-MP")]
        # CLOG is an explicit cache-test initialization dependency, not an
        # include directory used by the production main template.
        if name == "cycles_image_cache_test.cc":
            scan += ["-I" + str(fork / "intern/clog")]
        scan += ["-M", "-MF", str(dep), "-MT", "test-header-discovery"]
        call(scan, Path(name).stem + ".discover")
        scans[name] = {"command": scan, "dependency_sha256": sha256(dep)}
        dependencies = make_dependencies(dep)
        closures[name] = []
        for path in dependencies:
            path = dependency_path(str(path), snapshot)
            digest = sha256(path)
            closures[name].append({"path": str(path), "sha256": digest})
            if path.is_relative_to(fork):
                relative = str(path.relative_to(fork))
                if relative in admitted["blender_source_sha256"]:
                    if digest != admitted["blender_source_sha256"][relative]:
                        raise ValueError("Discovery fork differs from production: " + relative)
                    continue
                if relative in records:
                    if records[relative]["sha256"] != digest:
                        raise ValueError("Test header changed during discovery: " + relative)
                    continue
                if path.resolve().is_relative_to(fork.resolve()):
                    pinned, origin = pinned_test_header(fork, relative, gitlinks)
                    if hashlib.sha256(pinned).hexdigest() != digest or path.read_bytes() != pinned:
                        raise ValueError("Test-only fork header differs from pin: " + relative)
                else:
                    # Blender lib/ can lexically name an external SDK header.
                    origin = {"kind": "sdk-absolute", "source": str(path.resolve())}
                target = supplement / relative
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(path, target)
                if sha256(path) != digest or sha256(target) != digest:
                    raise ValueError("Test header changed while freezing: " + relative)
                records[relative] = {**origin, "sha256": digest}
            elif path.is_relative_to(build):
                relative = str(path.relative_to(build))
                if admitted["generated_sha256"].get(relative) != digest:
                    raise ValueError("Unqualified generated test header: " + relative)
            elif path.is_relative_to(frozen):
                relative = str(path.relative_to(frozen))
                if native_sha256.get(relative) != digest:
                    raise ValueError("Unqualified native test header: " + relative)
            else:
                external[str(path)] = digest
    record = {"schema": 1, "kind": "test-only-header-supplement", "blender_pin": PIN,
              "production_manifest_sha256": sha256(snapshot / "integrated-snapshot.json"),
              "production_context_sha256": production_headers,
              "headers": records, "external_sha256": external, "discovery": closures,
              "discovery_commands": scans}
    (out / "test-headers.json").write_text(json.dumps(record, indent=2) + "\n")
    return record


def validate_test_headers(snapshot, out, record):
    parent = json.loads((snapshot / "integrated-snapshot.json").read_text())
    if record.get("production_context_sha256") != parent["blender_source_sha256"]:
        raise ValueError("Incomplete frozen production test header context")
    for relative, digest in record["production_context_sha256"].items():
        if sha256(out / "test-headers/blender" / relative) != digest:
            raise ValueError("Stale frozen production test header: " + relative)
    for relative, info in record["headers"].items():
        if sha256(out / "test-headers/blender" / relative) != info["sha256"]:
            raise ValueError("Stale supplemental test header: " + relative)
    for path, digest in record["external_sha256"].items():
        if sha256(path) != digest:
            raise ValueError("Stale test compiler/SDK header: " + path)
    if record["production_manifest_sha256"] != sha256(snapshot / "integrated-snapshot.json"):
        raise ValueError("Changed test header production parent")


def dependency_scan(args, dep):
    scan = list(args)
    for flag in ("-o", "-MF", "-MT"):
        if flag in scan:
            index = scan.index(flag)
            del scan[index:index + 2]
    scan = [value for value in scan if value not in ("-c", "-MD", "-MMD", "-MP")]
    return scan + ["-M", "-MF", str(dep), "-MT", "frozen-test-headers"]


def controlled_overlay(snapshot, repo):
    original = json.loads((snapshot / "integrated-snapshot.json").read_text())["native_source_sha256"]
    return {relative: {"original_sha256": original.get(relative), "sha256": sha256(repo / relative)}
            for relative in sorted(CONTROLLED_TEST_SOURCES)}


def validate_overlay_copy(out, admitted, overlay, burst=False, displacement=False):
    root = out / ("displacement-source" if displacement else "burst-source" if burst else "controlled-source")
    expected = {**admitted["native_source_sha256"], **{name: info["sha256"] for name, info in overlay.items()}}
    actual = {str(path.relative_to(root)) for path in (root / "native").rglob("*") if path.is_file() and path.suffix in SOURCE_SUFFIXES}
    if actual != set(expected):
        raise ValueError("Incomplete controlled test source copy")
    for relative, digest in expected.items():
        if sha256(root / relative) != digest:
            raise ValueError("Stale controlled source copy: " + relative)
    return expected


def controlled_parent(path, admitted, snapshot):
    """Bind a prior default artifact without pretending its old helpers are current."""
    path = Path(path).resolve()
    parent = json.loads(path.read_text())
    if Path(parent.get("integrated_snapshot", "")).resolve() != snapshot.resolve() or parent.get("profile", "default") != "default":
        raise ValueError("Controlled parent must be a default integrated artifact")
    for relative, digest in admitted["native_source_sha256"].items():
        if parent.get("source_sha256", {}).get(relative) != digest:
            raise ValueError("Controlled parent native identity differs: " + relative)
    for item in parent.get("parents", []):
        if sha256(item["path"]) != item["sha256"]:
            raise ValueError("Stale controlled default parent")
    if not any(item["path"] == str(Path(parent["integrated_snapshot"]) / "integrated-snapshot.json") and
               item["sha256"] == sha256(Path(parent["integrated_snapshot"]) / "integrated-snapshot.json") for item in parent.get("parents", [])):
        raise ValueError("Controlled parent lacks production attestation")
    binaries = [(Path(name), digest) for name, digest in parent.get("binaries", {}).items() if Path(name).name == "cycles-cache-cpu"]
    if len(binaries) != 1 or sha256(binaries[0][0]) != binaries[0][1]:
        raise ValueError("Stale controlled parent CPU cache artifact")
    return {"path": str(path), "sha256": sha256(path)}, binaries[0]


def build_drivers(snapshot, repo, out, udim, env, controlled=False, parent_manifest=None, burst=False, controlled_parent_manifest=None, previous_attempt=None, displacement=False):
    """Reuse all admitted production objects; compile test translation units only."""
    snapshot, repo, out = map(lambda p: Path(p).resolve(), (snapshot, repo, out))
    if sum(map(bool, (burst, controlled, displacement))) > 1:
        raise ValueError("Choose one explicit diagnostic profile")
    if displacement and (udim or parent_manifest or controlled_parent_manifest or previous_attempt):
        raise ValueError("Displacement uses its own 128 fixture/full production parent only")
    if displacement:
        original = json.loads((snapshot / "integrated-snapshot.json").read_text())["native_source_sha256"]
        overlay = {relative: {"original_sha256": original.get(relative), "sha256": sha256(repo / relative)}
                   for relative in sorted(DISPLACEMENT_TEST_SOURCES)}
    elif burst:
        from cycles_burst_snapshot import PROFILE, overlay as burst_overlay
        overlay = burst_overlay(snapshot, repo)
    else:
        overlay = controlled_overlay(snapshot, repo) if controlled else {}
    profile = DISPLACEMENT_PROFILE if displacement else PROFILE if burst else CONTROLLED_PROFILE if controlled else "default"
    admitted = validate(snapshot, repo, overlay, profile)
    runner_sources = runner_identities(repo, burst, displacement)
    commands = json.loads((snapshot / "host-commands.json").read_text())
    templates = [shlex.split(command) for command in commands if " -c " in command]
    template = next(args for args in templates if args[args.index("-c") + 1].endswith("/native/host/main.cc"))
    native_root = template[template.index("-c") + 1].split("/native/", 1)[0] + "/native"
    build = admitted["source_build"]
    source = snapshot / "integration-source"
    parent = None
    controlled_record = None
    previous_records = []
    native_sha256 = admitted["native_source_sha256"]
    if controlled or burst:
        if not udim or parent_manifest is None:
            raise ValueError("Controlled diagnostic requires UDIM and explicit default parent manifest")
        parent, cache_parent = controlled_parent(parent_manifest, admitted, snapshot)
        if burst:
            from cycles_burst_snapshot import validate_controlled_parent, validate_previous_attempt
            controlled_record = validate_controlled_parent(controlled_parent_manifest, admitted, snapshot, parent)
            previous_records = validate_previous_attempt(previous_attempt)
    if controlled or burst or displacement:
        source = out / ("displacement-source" if displacement else "burst-source" if burst else "controlled-source")
        if source.exists():
            raise ValueError("Controlled source output exists; choose a fresh build output")
        shutil.copytree(snapshot / "integration-source", source)
        for relative in overlay:
            (source / relative).parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(repo / relative, source / relative)
        native_sha256 = validate_overlay_copy(out, admitted, overlay, burst, displacement)
    objects = out / "objects"
    objects.mkdir(exist_ok=True)
    (out / "bin").mkdir(exist_ok=True)
    compile_records = []

    def call(args, label):
        with (out / (label + ".log")).open("w") as log:
            subprocess.run(["nice", "-n", "19", *args], cwd=snapshot, env=env, stdout=log, stderr=subprocess.STDOUT, check=True)

    names = ["cycles_live_udim_artist_main.cc", "cycles_live_udim_artist_test.cc"] if udim else ["cycles_live_artist_test.cc"]
    if displacement:
        names = ["cycles_live_displacement_main.cc", "cycles_live_displacement_artist_test.cc", "CyclesLiveDisplacementProbe.cc"]
    elif burst:
        from cycles_burst_snapshot import COMPILED_TEST_NAMES
        names = list(COMPILED_TEST_NAMES)
    else:
        names.append("CyclesLiveCompletionProbe.cc" if controlled else "cycles_image_cache_test.cc")
    templates_by_name = {name: template for name in names}
    if displacement:
        actor_templates = [args for args in templates if args[args.index("-c") + 1].endswith("/native/harness/GuiFixDemo.cc")]
        if len(actor_templates) != 1:
            raise ValueError("Missing exact frozen GuiFixDemo compile template")
        templates_by_name["cycles_live_displacement_artist_test.cc"] = actor_templates[0]
        actor_object = actor_templates[0][actor_templates[0].index("-o") + 1]
    if controlled or displacement:
        cycles = [args for args in templates if args[args.index("-c") + 1].endswith("/intern/cycles/blender/session.cpp")]
        if len(cycles) != 1:
            raise ValueError("Missing exact frozen Cycles session compile template")
        templates_by_name["CyclesLiveDisplacementProbe.cc" if displacement else "CyclesLiveCompletionProbe.cc"] = cycles[0]
    discovery_commands = {}
    for name in names:
        args = [value.replace(native_root, str(source / "native")) for value in templates_by_name[name]]
        args[args.index("-c") + 1] = str(source / "native/tests" / name)
        if name not in ("CyclesLiveCompletionProbe.cc", "CyclesLiveDisplacementProbe.cc"):
            args += ["-I" + str(source / "native/host"), "-g0"]
            if controlled:
                args += ["-DBTS_BA_CONTROLLED_PROFILE_TEST=1"]
        discovery_commands[name] = args
    supplement = freeze_test_headers(snapshot, out, admitted, discovery_commands, call, source, native_sha256)

    frozen_commands = {}
    for name in names:
        path = source / "native/tests" / name
        obj = objects / (Path(name).stem + ".o")
        args = [v.replace(native_root, str(source / "native"))
                .replace(admitted["blender_root"], str(snapshot / "integration-blender"))
                .replace(build, str(snapshot / "integration-generated")) for v in templates_by_name[name]]
        args[args.index("-c") + 1] = str(path)
        args[args.index("-o") + 1] = str(obj)
        for flag, value in (("-MF", str(obj) + ".d"), ("-MT", str(obj))):
            if flag in args:
                args[args.index(flag) + 1] = value
        if name not in ("CyclesLiveCompletionProbe.cc", "CyclesLiveDisplacementProbe.cc"):
            args += ["-I" + str(source / "native/host"), "-g0"]
            if controlled:
                args += ["-DBTS_BA_CONTROLLED_PROFILE_TEST=1"]
        # Preserve original order/flags for concatenated -I and split
        # -isystem alike. One complete attested context preserves relative
        # includes across production and newly needed test-only headers.
        args = [value.replace(str(snapshot / "integration-blender"), str(out / "test-headers/blender")) for value in args]
        if name == "cycles_image_cache_test.cc":
            args += ["-I" + str(out / "test-headers/blender/intern/clog")]
        frozen_commands[name] = args
    # Verify the complete relocated preprocessor closure for every driver
    # before the first object compilation. No mutable fork/build fallback is
    # present in these commands, even for test-only initialization headers.
    allowed = {str(source / path): digest for path, digest in native_sha256.items()}
    allowed.update({str(snapshot / "integration-blender" / path): digest for path, digest in admitted["blender_source_sha256"].items()})
    allowed.update({str(snapshot / "integration-generated" / path): digest for path, digest in admitted["generated_sha256"].items()})
    allowed.update({str(out / "test-headers/blender" / path): info["sha256"] for path, info in supplement["headers"].items()})
    allowed.update({str(out / "test-headers/blender" / path): digest for path, digest in supplement["production_context_sha256"].items()})
    allowed.update(supplement["external_sha256"])
    frozen_scans = {}
    for name, args in frozen_commands.items():
        dep = out / (Path(name).stem + ".frozen.d")
        command = dependency_scan(args, dep)
        call(command, Path(name).stem + ".frozen-discover")
        frozen_scans[name] = {"command": command, "dependency_sha256": sha256(dep)}
        for path in make_dependencies(dep):
            path = dependency_path(str(path), snapshot)
            if allowed.get(str(path)) != sha256(path):
                raise ValueError("Unadmitted final test dependency: " + str(path))

    def compile_test(name):
        path = source / "native/tests" / name
        obj = objects / (Path(name).stem + ".o")
        args = frozen_commands[name]
        call(args, Path(name).stem + ".compile")
        compile_records.append({"source": "native/tests/" + name, "source_sha256": sha256(path), "object": str(obj), "object_sha256": sha256(obj), "command": args,
                                "original_production_compile_template": templates_by_name[name]})
        return str(obj)

    driver = compile_test("cycles_live_displacement_main.cc" if displacement else "cycles_live_burst_main.cc" if burst else "cycles_live_udim_artist_main.cc" if udim else "cycles_live_artist_test.cc")
    case = compile_test("cycles_live_displacement_artist_test.cc") if displacement else compile_test("cycles_live_burst_test.cc" if burst else "cycles_live_udim_artist_test.cc") if udim else None
    observer = compile_test("CyclesLiveDisplacementProbe.cc" if displacement else "CyclesLiveCompletionProbe.cc") if controlled or displacement else None
    cache = None if controlled or burst or displacement else compile_test("cycles_image_cache_test.cc")
    args = link_arguments(commands)
    main = next(v for v in args if v.endswith("/main.cc.o"))
    if args.count(main) != 1:
        raise ValueError("Ambiguous production main replacement")
    link = []
    for value in args:
        if value == main:
            value = driver
        elif displacement and value == actor_object:
            value = case
        elif value in admitted["link_inputs"]:
            info = admitted["link_inputs"][value]
            value = str(snapshot / info["path"]) if info["kind"] == "frozen" else info["path"]
        elif value.startswith("-Wl,--dependency-file="):
            value = "-Wl,--dependency-file=" + str(out / "integrated-link.d")
        elif value in admitted["linker_scripts"]:
            value = "-Wl,--version-script=" + str(snapshot / "integration-blender" / admitted["linker_scripts"][value])
        else:
            value = value.replace(build + "/bin/lib", str(snapshot / "bin/lib"))
        link.append(value)
    if displacement and sum(v == case for v in link) != 1:
        raise ValueError("Expected exactly one Displacement actor replacement")
    if case and not displacement:
        link.insert(next(i for i, v in enumerate(link) if v.endswith(".a")), case)
    if observer:
        link.insert(next(i for i, v in enumerate(link) if v.endswith(".a")), observer)
    links = []
    for name, main_object in (("cycles-live-artist", driver), ("cycles-cache-cpu", cache)):
        if displacement and name == "cycles-cache-cpu":
            continue
        if (controlled or burst) and name == "cycles-cache-cpu":
            binary = out / "bin" / name
            shutil.copyfile(cache_parent[0], binary)
            shutil.copymode(cache_parent[0], binary)
            if sha256(binary) != cache_parent[1]:
                raise ValueError("Parent CPU cache changed while retaining")
            links.append({"binary": str(binary), "sha256": cache_parent[1], "retained_from_default_parent": str(cache_parent[0])})
            continue
        command = [main_object if v == driver else v for v in link]
        # The cache driver has no dependency on the optional UDIM artist case.
        if name == "cycles-cache-cpu" and case:
            command.remove(case)
        binary = out / "bin" / name
        command[command.index("-o") + 1] = str(binary)
        call(command, name + ".link")
        links.append({"binary": str(binary), "sha256": sha256(binary), "command": command})
    # Recheck all identities after compiler/linker work; no output can be
    # relabelled current if a frozen production input/source changed mid-build.
    validate(snapshot, repo, overlay, profile)
    if controlled or burst or displacement:
        validate_overlay_copy(out, admitted, overlay, burst, displacement)
    if burst:
        if validate_previous_attempt(previous_attempt) != previous_records:
            raise ValueError("Original three-second evidence changed during six-second build")
        if validate_controlled_parent(controlled_parent_manifest, admitted, snapshot, parent) != controlled_record:
            raise ValueError("Controlled parent changed during burst build")
        if controlled_parent(parent_manifest, admitted, snapshot)[0] != parent:
            raise ValueError("Default parent changed during burst build")
    validate_test_headers(snapshot, out, supplement)
    validate_runner_sources(repo, runner_sources, burst, displacement)
    resources = snapshot / "bin/5.2"
    if not resources.is_dir():
        raise ValueError("Integrated snapshot must retain its Blender 5.2 runtime resources")
    if not (out / "bin/5.2").exists():
        (out / "bin/5.2").symlink_to(resources)
    record = {"kind": "integrated-production-test-drivers-only", "runner_source_sha256": runner_sources, "snapshot_manifest": str(snapshot / "integrated-snapshot.json"),
              "profile": profile, "test_overlay": overlay, "default_parent": parent, "controlled_parent": controlled_record, "previous_attempt_evidence": previous_records,
              "snapshot_manifest_sha256": sha256(snapshot / "integrated-snapshot.json"), "production_source_head": admitted["native_source_head"],
              "test_header_manifest_sha256": sha256(out / "test-headers.json"),
              "frozen_preprocessor_closure": frozen_scans,
              "compiled_test_units": compile_records, "production_units_recompiled": [], "fork_patches_reapplied": False, "links": links}
    (out / "build.json").write_text(json.dumps(record, indent=2) + "\n")
    (out / "run-manifest.json").write_text(json.dumps({"schema": 1, "mode": "displacement" if displacement else "udim" if udim else "128", "sanitized_bridge_only": False,
        "source_sha256": {**native_sha256, **runner_sources}, "patch_sha256": {k: v for k, v in admitted["patch_series_sha256"].items() if Path(k).name.startswith(("0035-", "0036-", "0037-", "0038-", "0040-", "0041-"))},
        "profile": profile, "test_overlay": overlay,
        "parents": [*([parent] if parent else []), *([controlled_record] if controlled_record else []), *previous_records, {"path": str(out / "build.json"), "sha256": sha256(out / "build.json")}, {"path": str(out / "test-headers.json"), "sha256": sha256(out / "test-headers.json")}, {"path": str(snapshot / "integrated-snapshot.json"), "sha256": sha256(snapshot / "integrated-snapshot.json")}],
        "integrated_snapshot": str(snapshot), "binaries": {entry["binary"]: entry["sha256"] for entry in links}}, indent=2) + "\n")
    return record
