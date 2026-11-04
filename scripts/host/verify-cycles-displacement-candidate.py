"""Admit only the reviewed, selective0037 displacement prototype.

The ordinary runner and its full36-patch baseline admission stay unchanged.
This wrapper does not compile, patch installed sources, or attest a complete
0037 rebuild. Its only runtime is the existing displacement artist profile.
"""
import argparse
import copy
import hashlib
import json
import mmap
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile

from cycles_live_manifest import sha256, validate_manifest
import cycles_integrated_snapshot as integrated


BASE_BUILD = "541fbad7d7a2564f3d4ad6c401ecbee1cc649c65579d889a335951d7f8afbcac"
BASE_RUN = "da1b98395b655fd858d7d6075907b1545ef96dcfda2807f6719a989d148d6c8b"
BASE_PARENT = "68cd81d4f595295d3ca7c8b57bb7165f360188505306287d19a03fcd4d55d95c"
PATCH = "986e66d6abf2ca427aa9023670d2282abb1da88376e6e5ef8afb87e4e8ce3210"
FORK_TU_HEADERS = "bf815beab799703fb19f2a4d20af3337f595ca3e45011b1734cb9b76a873456c"
COMPILE_ADMISSION = "9993210702086cf7c8ef91d848f41892126bf49f8d314ce583da37aaa857e6b5"
PRELINK_FAILURE = "0bd157175b5fb00b1415854d6d66dce46131663f2a8f7663766c000199a6684d"
CANDIDATE_BUILD = "fff3351e8f3c8f214c4e2e35267e4bbc822acc7e87d7299e0f9a25836428af62"
FORK_FILES = {"intern/cycles/scene/image.h", "intern/cycles/scene/image.cpp",
              "intern/cycles/blender/sync.cpp"}
ARCHIVE_MEMBERS = {"libcycles_scene.a": "image.cpp.o",
                   "libbf_intern_cycles.a": "sync.cpp.o"}
TEST_UNITS = {"native/tests/cycles_live_displacement_main.cc",
              "native/tests/cycles_live_displacement_artist_test.cc",
              "native/tests/CyclesLiveDisplacementProbe.cc"}


def require(condition, message):
    if not condition:
        raise ValueError(message)


def file_record(record):
    path = Path(record["path"]).resolve()
    require(sha256(path) == record["sha256"], "Stale candidate artifact: " + str(path))
    return path


def verify_fixed_parents(record, baseline):
    require(sha256(baseline / "build.json") == record["baseline_build_sha256"] == BASE_BUILD
            and sha256(baseline / "run-manifest.json") == BASE_RUN
            and record["parent_manifest_sha256"] == BASE_PARENT and record["patch_sha256"] == PATCH,
            "Candidate is not the exact reviewed baseline/0037 patch")


def archive_members(path):
    """Hash real GNU ar members; symbol tables are metadata, never object bytes."""
    result, long_names = {}, b""
    with Path(path).open("rb") as file, mmap.mmap(file.fileno(), 0, access=mmap.ACCESS_READ) as raw:
        require(raw[:8] == b"!<arch>\n", "Expected a regular GNU ar archive")
        offset = 8
        while offset < len(raw):
            header = raw[offset:offset + 60]
            require(len(header) == 60 and header[58:60] == b"`\n", "Invalid ar member header")
            try:
                size = int(header[48:58])
            except ValueError as error:
                raise ValueError("Invalid ar member size") from error
            begin, end = offset + 60, offset + 60 + size
            require(size >= 0 and end <= len(raw), "Truncated ar member")
            name = header[:16].decode("ascii").rstrip()
            if name == "//":
                require(not long_names, "Duplicate GNU ar name table")
                long_names = raw[begin:end]
            elif name not in {"/", "/SYM64/"}:
                if name.startswith("/"):
                    require(name[1:].isdigit(), "Unknown GNU ar member spelling")
                    index = int(name[1:])
                    tail = long_names[index:]
                    stop = tail.find(b"/\n")
                    require(index < len(long_names) and stop >= 0, "Invalid GNU ar long name")
                    name = tail[:stop].decode("utf-8")
                else:
                    require(name.endswith("/") and not name.startswith("#1/"), "Unsupported ar name")
                    name = name[:-1]
                require(name and name not in result, "Duplicate/empty ar member")
                view = memoryview(raw)[begin:end]
                try:
                    result[name] = hashlib.sha256(view).hexdigest()
                finally:
                    view.release()
            offset = end + (size & 1)
        require(offset == len(raw) and result, "Incomplete/empty ar archive")
    return result


def verify_archive(original, candidate, member, object_path):
    before, after = archive_members(original), archive_members(candidate)
    require(set(before) == set(after) and member in before, "Candidate archive changed member identity")
    require({name for name in before if before[name] != after[name]} == {member},
            "Candidate archive must change exactly its reviewed member")
    require(after[member] == sha256(object_path), "Replaced ar member is not the compiled object")
    return ({"member": member, "unchanged_members": len(before) - 1,
             "original_member_sha256": before[member], "candidate_member_sha256": after[member]},
            {"original_members_sha256": before, "candidate_members_sha256": after})


def verify_relink(original, actual, replacements, original_binary, candidate_binary):
    """No added flags, objects or libraries: two archives, output and link-dep path."""
    require(len(replacements) == 2 and len(set(replacements.values())) == 2,
            "Expected exactly two private archive replacements")
    expected = list(original)
    for old, new in replacements.items():
        require(expected.count(old) >= 1, "Baseline link lacks reviewed archive")
        expected = [new if argument == old else argument for argument in expected]
    output = expected.index("-o") + 1
    require(expected[output] == str(original_binary), "Unexpected baseline link output")
    expected[output] = str(candidate_binary)
    old_dep = str(Path(original_binary).parent.parent / "integrated-link.d")
    new_dep = str(Path(candidate_binary).parent.parent / "candidate.link.d")
    expected = [argument.replace(old_dep, new_dep) if argument == "-Wl,--dependency-file=" + old_dep
                else argument for argument in expected]
    require(actual == expected, "Candidate link differs beyond its two archives/output/dependency path")
    return expected


def verify_test_units(baseline, reused):
    require(len(baseline) == len(reused) == 3 and {unit["source"] for unit in baseline} == TEST_UNITS,
            "Expected the original three displacement test objects")
    originals = {unit["source"]: unit for unit in baseline}
    require({unit["source"] for unit in reused} == TEST_UNITS, "Changed/duplicate candidate test units")
    for unit in reused:
        old = originals[unit["source"]]
        require(unit == old,
                "Candidate did not reuse exact baseline test object")
        require(sha256(unit["object"]) == unit["object_sha256"], "Stale reused displacement test object")


def artifact_root(record):
    roots = set()
    for unit in record["compiled_units"]:
        suffix = "/objects/" + str(Path(unit["source"]).with_suffix(".cpp.o"))
        require(unit["source"] in FORK_FILES and unit["object"].endswith(suffix), "Unexpected compiled artifact layout")
        roots.add(Path(unit["object"][:-len(suffix)]).resolve())
    require(len(roots) == 1, "Both Cycles objects must share one retained compile context")
    return roots.pop()


def verify_finalization(record, artifacts, base):
    final = record["finalization"]
    require(set(final) == {"kind", "successful_compile_parent", "retained_prelink_failure_sha256",
                          "archive_token_multiplicity", "archive_inventory_files", "new_compile_count"}
            and final["kind"] == "link-only" and final["new_compile_count"] == 0
            and Path(final["successful_compile_parent"]).resolve() == artifacts,
            "Unknown or non-link-only candidate finalization")
    failure = artifacts / "build-result.json"
    require(sha256(failure) == final["retained_prelink_failure_sha256"] == PRELINK_FAILURE
            and sha256(artifacts / "admission.json") == COMPILE_ADMISSION,
            "Changed retained compile admission/prelink failure")
    for path, digest in ((failure, PRELINK_FAILURE), (artifacts / "admission.json", COMPILE_ADMISSION)):
        require(record["parents"].get(str(path)) == digest, "Missing retained compile/failure parent")
    old = json.loads(failure.read_text())
    require(old["actual_exit"] == 1 and old["phase"] == "link", "Retained failure was not the prelink wrapper guard")
    expected = {archive["original"]: base["links"][0]["command"].count(archive["original"])
                for archive in record["archives"]}
    require(final["archive_token_multiplicity"] == expected and sorted(expected.values()) == [2, 3],
            "Candidate lost baseline archive multiplicities")
    inventories = {str(artifacts / (archive["member"] + ".archive-members.json")): archive["member_inventory_sha256"]
                   for archive in record["archives"]}
    require(len(final["archive_inventory_files"]) == 2
            and {item["path"]: item["sha256"] for item in final["archive_inventory_files"]} == inventories,
            "Link-only finalization did not retain both original archive inventories")


def verify_fork_supplement(record, snapshot, parent):
    item = record["fork_tu_header_supplement"]
    require(set(item) == {"path", "sha256", "scope"} and item["scope"] == "fork-TU-extraheaders"
            and item["sha256"] == FORK_TU_HEADERS, "Unknown fork-TU header supplement")
    path = file_record(item)
    supplement = json.loads(path.read_text())
    integrated.validate_test_headers(snapshot, path.parent, supplement)
    sources = FORK_FILES - {"intern/cycles/scene/image.h"}
    require(set(supplement["discovery"]) == set(supplement["discovery_commands"]) == sources,
            "Fork supplement must discover only the two original36 Cycles units")
    commands = json.loads((snapshot / "host-commands.json").read_text())
    for source in sources:
        original_source = str(Path(parent["blender_root"]) / source)
        originals = [shlex.split(command) for command in commands if original_source in command and " -c " in command]
        require(len(originals) == 1, "Missing original fork-TU command")
        expected = list(originals[0])
        for flag in ("-o", "-MF", "-MT"):
            if flag in expected:
                index = expected.index(flag); del expected[index:index + 2]
        expected = [value for value in expected if value not in ("-c", "-MD", "-MMD", "-MP")]
        dependency = path.parent / (Path(source).stem + ".discovery.d")
        expected += ["-M", "-MF", str(dependency), "-MT", "test-header-discovery"]
        scan = supplement["discovery_commands"][source]
        require(scan["command"] == expected and sha256(dependency) == scan["dependency_sha256"],
                "Fork supplement did not use exact original36 source/flags")
        consumed = {entry["path"]: entry["sha256"] for entry in supplement["discovery"][source]}
        require(consumed.get(original_source) == parent["blender_source_sha256"][source],
                "Fork discovery source differs from full36 parent")
        require({str(integrated.dependency_path(str(value), snapshot))
                 for value in integrated.make_dependencies(dependency)} == set(consumed),
                "Fork supplement dependency membership differs")
    require(record["parents"].get(str(path)) == FORK_TU_HEADERS, "Fork supplement lacks exact candidate parent")
    return supplement


def verify_compiles(record, root, snapshot, parent, baseline_headers, supplement=None):
    commands = json.loads((snapshot / "host-commands.json").read_text())
    units = record["compiled_units"]
    require(len(units) == 2 and {unit["source"] for unit in units} == FORK_FILES - {"intern/cycles/scene/image.h"},
            "Expected only the two reviewed Cycles translation units")
    allowed = {str(root / "blender" / relative): digest
               for relative, digest in parent["blender_source_sha256"].items()}
    allowed.update({str(root / "blender" / relative): info["sha256"]
                    for relative, info in baseline_headers["headers"].items()})
    allowed.update(baseline_headers["external_sha256"])
    allowed.update({str(snapshot / "integration-generated" / relative): digest
                    for relative, digest in parent["generated_sha256"].items()})
    if supplement is not None:
        extras = {str(root / "blender" / relative): info["sha256"] for relative, info in supplement["headers"].items()}
        extras.update(supplement["external_sha256"])
        require(all(path not in allowed or allowed[path] == digest for path, digest in extras.items()),
                "Conflicting original36 header contexts")
        allowed.update(extras)
    allowed.update({source["patched_path"]: source["patched_sha256"] for source in record["sources"]})
    for path, digest in allowed.items():
        require(sha256(path) == digest, "Changed private compile context: " + path)
    for unit in units:
        original_source = str(Path(parent["blender_root"]) / unit["source"])
        originals = [shlex.split(command) for command in commands
                     if original_source in command and " -c " in command]
        require(len(originals) == 1 and unit["original_command"] == originals[0],
                "Candidate compile template is not the full parent's exact command")
        mappings = [(parent["blender_root"], str(root / "blender")),
                    (parent["source_build"], str(snapshot / "integration-generated"))]
        def relocate(argument):
            for source, target in sorted(mappings, key=lambda item: -len(item[0])):
                for flag in ("-I", "-isystem", "-iquote", ""):
                    value = argument[len(flag):] if flag and argument.startswith(flag) else argument if not flag else None
                    if value is not None and (value == source or value.startswith(source + "/")):
                        return flag + target + value[len(source):]
            return argument
        expected = [relocate(argument) for argument in originals[0]]
        obj = (root / "objects" / unit["source"]).with_suffix(".cpp.o")
        require(unit["object"] == str(obj), "Unexpected private compiled object path")
        expected[expected.index("-o") + 1] = str(obj)
        expected[expected.index("-c") + 1] = str(root / "blender" / unit["source"])
        for flag, value in (("-MF", str(obj) + ".d"), ("-MT", str(obj))):
            if flag in expected:
                expected[expected.index(flag) + 1] = value
        require(unit["command"] == expected, "Changed Cycles compiler flags or private source")
        require(unit["compile"]["actual_exit"] == 0 and unit["compile"]["command"] == expected,
                "Missing successful exact Cycles compile receipt")
        file_record({"path": unit["compile"]["log"], "sha256": unit["compile"]["log_sha256"]})
        require(sha256(obj) == unit["object_sha256"], "Changed compiled Cycles object")
        require(ARCHIVE_MEMBERS.get(Path(unit["archive"]).name) == unit["member"], "Wrong Cycles archive/member pair")
        closure_file = root / (unit["member"] + ".consumed.json")
        require(sha256(closure_file) == unit["closure_sha256"], "Changed frozen-header discovery receipt")
        closure = json.loads(closure_file.read_text())
        actual = {str(path): sha256(path) for path in
                  (integrated.dependency_path(str(path), snapshot)
                   for path in integrated.make_dependencies(Path(str(obj) + ".d")))}
        require(actual == closure and str(root / "blender" / unit["source"]) in actual,
                "Actual compiled closure differs from discovery/source")
        require(all(allowed.get(path) == digest for path, digest in closure.items()),
                "Cycles compile consumed an unadmitted source/header")


def validate_candidate(candidate, baseline, repo):
    """Validate the fixed prototype before and after its unchanged artist runner."""
    candidate, baseline, repo = map(lambda path: Path(path).resolve(), (candidate, baseline, repo))
    record = json.loads((candidate / "build.json").read_text())
    fields = {"schema", "kind", "parents", "parent_manifest_sha256", "baseline_build_sha256",
              "patch_sha256", "sources", "compiled_units", "archives", "reused_test_units",
              "link_command_sha256", "link", "binary", "binary_sha256", "source_and_input_guards_passed",
              "installed_fork_or_artist_changed", "native_runtime_started", "full_integration_claim", "fork_tu_header_supplement", "finalization"}
    require(set(record) == fields and record["schema"] == 1 and record["kind"] == "selective-displacement37-prototype",
            "Unknown selective prototype receipt schema")
    require(sha256(candidate / "build.json") == CANDIDATE_BUILD, "Not the one reviewed selective0037 build receipt")
    require(record["source_and_input_guards_passed"] is True and record["installed_fork_or_artist_changed"] is False
            and record["native_runtime_started"] is False and record["full_integration_claim"] is False,
            "Candidate must be successful build-only, uninstalled and explicitly not full integration")
    verify_fixed_parents(record, baseline)
    base = json.loads((baseline / "build.json").read_text())
    artifacts = artifact_root(record)
    require(artifacts.parent == candidate.parent and artifacts != candidate,
            "Link-only candidate must retain its separate sibling compile context")
    snapshot = Path(base["snapshot_manifest"]).resolve().parent
    require(sha256(snapshot / "integrated-snapshot.json") == BASE_PARENT, "Changed full36 production parent")
    parent = integrated.validate(snapshot, repo, base["test_overlay"], integrated.DISPLACEMENT_PROFILE)
    validate_manifest(baseline / "run-manifest.json", repo, baseline / "bin/cycles-live-artist",
                      "displacement", False, integrated.DISPLACEMENT_PROFILE)
    integrated.validate_overlay_copy(baseline, parent, base["test_overlay"], displacement=True)
    integrated.validate_runner_sources(repo, base["runner_source_sha256"], displacement=True)
    headers = json.loads((baseline / "test-headers.json").read_text())
    integrated.validate_test_headers(snapshot, baseline, headers)
    result = json.loads((candidate / "build-result.json").read_text())
    require(result["actual_exit"] == 0 and result["phase"] == "complete"
            and result["build_sha256"] == sha256(candidate / "build.json"), "Candidate build did not complete successfully")
    for path, digest in record["parents"].items():
        require(sha256(path) == digest, "Changed candidate build input: " + path)
    required_parents = {baseline / "build.json": BASE_BUILD, baseline / "run-manifest.json": BASE_RUN,
                        snapshot / "integrated-snapshot.json": BASE_PARENT}
    normalized = {Path(path).resolve(): digest for path, digest in record["parents"].items()}
    require(all(normalized.get(path) == digest for path, digest in required_parents.items()), "Missing exact candidate parents")
    patches = [Path(path) for path, digest in record["parents"].items()
               if Path(path).name == "0037-cycles-hosted-displacement-image-refresh.patch" and digest == PATCH]
    require(len(patches) == 1, "Missing the one reviewed0037 patch")
    sources = record["sources"]
    require(len(sources) == 3 and {source["source"] for source in sources} == FORK_FILES,
            "Candidate must modify exactly the three reviewed fork files")
    with tempfile.TemporaryDirectory(prefix="cycles37-admission-") as directory:
        temporary = Path(directory)
        for source in sources:
            relative = source["source"]
            original = snapshot / "integration-blender" / relative
            require(source["original_sha256"] == parent["blender_source_sha256"][relative] == sha256(original),
                    "Patch source differs from full36 parent")
            target = temporary / relative; target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(original.read_bytes())
            require(Path(source["patched_path"]).resolve() == artifacts / "blender" / relative,
                    "Patched source escaped the private context")
        subprocess.run(["patch", "--batch", "--fuzz=0", "-p1", "-i", str(patches[0].resolve())],
                       cwd=temporary, check=True, stdout=subprocess.DEVNULL)
        for source in sources:
            require(sha256(temporary / source["source"]) == source["patched_sha256"] == sha256(source["patched_path"]),
                    "Private source is not exactly the reviewed patch applied to full36 parent")
    supplement = verify_fork_supplement(record, snapshot, parent)
    verify_compiles(record, artifacts, snapshot, parent, headers, supplement)
    verify_test_units(base["compiled_test_units"], record["reused_test_units"])
    archives = record["archives"]
    require(len(archives) == 2 and {Path(archive["original"]).name for archive in archives} == set(ARCHIVE_MEMBERS),
            "Expected exactly two reviewed archive overrides")
    replacements, members = {}, []
    for archive in archives:
        name = Path(archive["original"]).name
        require(Path(archive["original"]).resolve() == snapshot / "lib" / name
                and Path(archive["candidate"]).resolve() == artifacts / "archives" / name
                and archive["member"] == ARCHIVE_MEMBERS[name], "Wrong private archive/member identity")
        require(sha256(archive["original"]) == archive["original_sha256"]
                and sha256(archive["candidate"]) == archive["candidate_sha256"], "Stale baseline/private archive")
        unit = next(unit for unit in record["compiled_units"] if unit["member"] == archive["member"])
        require(archive["object_sha256"] == unit["object_sha256"] and archive["non_target_members_byte_identical"] is True,
                "Archive does not bind its reviewed compiled member")
        stats, actual_inventory = verify_archive(archive["original"], archive["candidate"], archive["member"], unit["object"])
        members.append(stats)
        inventory = artifacts / (archive["member"] + ".archive-members.json")
        require(sha256(inventory) == archive["member_inventory_sha256"], "Changed member inventory receipt")
        recorded = json.loads(inventory.read_text())
        require(recorded == actual_inventory, "False archive inventory receipt")
        replacements[archive["original"]] = archive["candidate"]
    require(len(base["links"]) == 1 and len(record["link"]["command"]) > 1, "Missing sole actor link")
    binary = candidate / "bin/cycles-live-artist"
    require(Path(record["binary"]).resolve() == binary and sha256(binary) == record["binary_sha256"], "Changed candidate binary")
    verify_relink(base["links"][0]["command"], record["link"]["command"], replacements,
                  base["links"][0]["binary"], binary)
    require(record["link"]["actual_exit"] == 0 and sha256(candidate / "link-command.json") == record["link_command_sha256"]
            and json.loads((candidate / "link-command.json").read_text()) == record["link"]["command"], "Missing actual successful link receipt")
    file_record({"path": record["link"]["log"], "sha256": record["link"]["log_sha256"]})
    verify_finalization(record, artifacts, base)
    return record, base, snapshot, members


def prepare_runtime(candidate, baseline, repo, record, base, snapshot):
    """Keep actual selective build.json separate from borrowed baseline context."""
    output = candidate / "runtime-driver"
    output.mkdir(exist_ok=True)
    (output / "bin").mkdir(exist_ok=True)
    for target, source in ((output / "displacement-source", baseline / "displacement-source"),
                           (output / "test-headers", baseline / "test-headers"),
                           (output / "bin/cycles-live-artist", candidate / "bin/cycles-live-artist"),
                           (output / "bin/5.2", snapshot / "bin/5.2"),
                           (output / "bin/lib", snapshot / "bin/lib")):
        if target.is_symlink():
            require(target.resolve() == source.resolve(), "Changed borrowed runtime context")
        else:
            require(not target.exists(), "Unexpected existing runtime context")
            target.symlink_to(source)
    headers = (baseline / "test-headers.json").read_bytes()
    build = {"kind": "selective0037-not-full-integration-runtime-admission", "schema": 1,
             "full36_baseline_build": {"path": str(baseline / "build.json"), "sha256": BASE_BUILD},
             "selective_candidate_build": {"path": str(candidate / "build.json"), "sha256": sha256(candidate / "build.json")},
             "full36_parent": {"path": str(snapshot / "integrated-snapshot.json"), "sha256": BASE_PARENT},
             "runner_source_sha256": base["runner_source_sha256"],
             "scope": "two private Cycles objects/archive members; same three baseline test objects; not a complete0037 rebuild"}
    manifest = copy.deepcopy(json.loads((baseline / "run-manifest.json").read_text()))
    wrapper_sources = ("scripts/host/verify-cycles-displacement-candidate.py",
                       "native/tests/cycles_displacement_candidate_test.py")
    manifest["source_sha256"].update({name: sha256(repo / name) for name in wrapper_sources})
    manifest["binaries"] = {str((output / "bin/cycles-live-artist").resolve()): record["binary_sha256"]}
    for path in (candidate / "build.json", candidate / "build-result.json", output / "build.json",
                 *(repo / name for name in wrapper_sources)):
        digest = hashlib.sha256((json.dumps(build, indent=2) + "\n").encode()).hexdigest() if path == output / "build.json" else sha256(path)
        manifest["parents"].append({"path": str(path), "sha256": digest})
    manifest["selective_prototype"] = {"kind": "reviewed0037-displacement-only", "patch_sha256": PATCH,
                                        "full_integration_claim": False,
                                        "full36_baseline_run_manifest_sha256": BASE_RUN,
                                        "candidate_build_sha256": sha256(candidate / "build.json")}
    for target, data in ((output / "test-headers.json", headers),
                         (output / "build.json", (json.dumps(build, indent=2) + "\n").encode()),
                         (output / "run-manifest.json", (json.dumps(manifest, indent=2) + "\n").encode())):
        if target.exists():
            require(target.read_bytes() == data, "Existing candidate runtime admission differs")
        else:
            target.write_bytes(data)
    return output


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--candidate", type=Path, required=True)
    parser.add_argument("--baseline", type=Path, required=True)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--admit-only", action="store_true", help="Validate/selective manifest only; no host process")
    mode.add_argument("--run-only", action="store_true", help="One separately authorized unchanged displacement actor")
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[2]
    candidate, baseline = args.candidate.resolve(), args.baseline.resolve()
    require(candidate.is_relative_to(repo.parent / "build") and candidate != baseline,
            "Candidate must be a separate private build directory")
    record, base, snapshot, members = validate_candidate(candidate, baseline, repo)
    output = prepare_runtime(candidate, baseline, repo, record, base, snapshot)
    require(not (output / "runs").exists(), "This selective candidate already has a runtime attempt; no automatic retry")
    receipt = {"kind": "selective0037-not-full-integration", "candidate_build_sha256": sha256(candidate / "build.json"),
               "baseline_build_sha256": BASE_BUILD, "baseline_run_manifest_sha256": BASE_RUN,
               "full36_parent_sha256": BASE_PARENT, "patch_sha256": PATCH, "members": members,
               "run_manifest_sha256": sha256(output / "run-manifest.json"), "runtime_started": args.run_only}
    (candidate / "runtime-admission.json").write_text(json.dumps(receipt, indent=2) + "\n")
    if args.admit_only:
        print(json.dumps(receipt, indent=2))
        return 0
    command = [sys.executable, str(repo / "scripts/host/verify-cycles-live.py"), "--snapshot", str(snapshot),
               "--output", str(output), "--integrated-snapshot", "--displacement-profile", "--run-only"]
    code = subprocess.run(command).returncode
    # Retain the delegated return code even if a later identity check fails.
    post = {**receipt, "actual_runner_exit": code, "post_identity_passed": False}
    try:
        validate_candidate(candidate, baseline, repo)
        validate_manifest(output / "run-manifest.json", repo, output / "bin/cycles-live-artist",
                          "displacement", False, integrated.DISPLACEMENT_PROFILE)
        require(sha256(output / "run-manifest.json") == receipt["run_manifest_sha256"], "Runtime manifest changed")
        post["post_identity_passed"] = True
    except BaseException as error:
        post["post_identity_error"] = repr(error)
        raise
    finally:
        (candidate / "runtime-post-admission.json").write_text(json.dumps(post, indent=2) + "\n")
    return code


if __name__ == "__main__":
    raise SystemExit(main())
