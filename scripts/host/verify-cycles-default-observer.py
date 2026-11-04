"""One explicit default-observation-only-v2 cohort; historical runners stay unchanged.

The BASE view attests only the original full37 production. The separately named
observer cohort replaces one old default actor and adds one readonly Cycles TU.
"""
import argparse
import concurrent.futures
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time

import cycles_integrated_snapshot as integration
from cycles_live_manifest import fresh_runtime, freeze_admission

PROFILE = "default-observation-only-v2"
PARENT = "0b49ff8abb3563ed2c2df0f6a4243773d44330965f5ce165e802d36416ee647c"
DEFAULT_BUILD = "9ad2c3065afc1da664306811dfdc1f811578bd44ba1d6d3ad1c45f1414a57e7e"
DEFAULT_RUN = "1b1e855928dcdbdecc9e1dc72feb70259d4c9e48b889c22aa14b17ef0be7a667"
PROBE_BUILD = "3113c891f17fdd260a468f1e133e9bbb87eaaf8121a9cfa20f06be0fbf923728"
SYNTAX = "17e164b762fa968ae397d753b993fb2b62b0ba40366d4b76b9137613b8a3f0ba"
SOURCES = {
    "native/tests/CyclesDefaultCompletionEpoch.hh": "b3acb72c0d64b924031a398b817e50f3a37310b38c5ca3529c13613c5f8f3bf0",
    "native/tests/CyclesDefaultCompletionProbe.hh": "0d910c939f2fddcb675e0267179b33df088d579717868698e13e382033521c2a",
    "native/tests/CyclesDefaultCompletionProbe.cc": "f9e5a1d3ecb50e9b1d830366f0aa8630c8172b1408e2988ccb707710ae067472",
    "native/tests/cycles_live_default_observed_artist_test.cc": "e3a8029676e729bf6f41a0995007f3d0945aea84a4550ce5b27e399e822d5255",
    "native/tests/cycles_default_completion_epoch_test.cc": "f720143a2e333de43d567251b4e126495f9b0775452cd951304c41e9ca09cf77",
    "native/tests/cycles_default_completion_observer_plan.md": "23e450277bff18d407aee4acf16d9689cb606c570fe4fafc3c2adaea870e732b",
}
PAIRS = {"actor": ("cycles_live_udim_artist_test.cc", "cycles_live_default_observed_artist_test.cc"),
         "probe": ("CyclesLiveDisplacementProbe.cc", "CyclesDefaultCompletionProbe.cc")}
sha = integration.sha256

def require(okay, message):
    if not okay:
        raise ValueError(message)

def read(path):
    return json.loads(Path(path).read_text())

def record(path):
    return {"path": str(Path(path).resolve()), "sha256": sha(path)}

def fixed_file(path, digest):
    require(sha(path) == digest, "Stale fixed cohort artifact: " + str(path))
    return read(path)

def source_guard(repo, parent):
    actual = {str(p.relative_to(repo)) for p in (repo / "native").rglob("*")
              if p.is_file() and p.suffix in integration.SOURCE_SUFFIXES}
    extras = {p for p in SOURCES if Path(p).suffix in integration.SOURCE_SUFFIXES}
    require(actual == set(parent["native_source_sha256"]) | extras, "Undeclared native source addition/removal")
    for relative, digest in {**parent["native_source_sha256"], **SOURCES}.items():
        require(sha(repo / relative) == digest, "Changed production or observer source: " + relative)

def production_base(snapshot, repo, out, parent, create):
    # Parent-only: never insert observer files then call it the old cohort.
    view = out / "production-base"
    if create:
        require(not view.exists(), "BASE output already exists")
        for relative, digest in parent["native_source_sha256"].items():
            original, target = snapshot / "integration-source" / relative, view / relative
            require(sha(original) == digest, "Stale frozen native source")
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(original, target)
        for relative, digest in {**parent["patch_series_sha256"],
                                 "scripts/host/verify-gpu-only-paths.py": parent["gpu_source_guard_sha256"]}.items():
            require(sha(repo / relative) == digest, "Stale production patch/guard")
            target = view / relative; target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(repo / relative, target)
    return integration.validate(snapshot, view)

def parents(options, repo, out, create):
    snapshot, baseline, probe = options.snapshot.resolve(), options.baseline.resolve(), options.probe_donor.resolve()
    parent = fixed_file(snapshot / "integrated-snapshot.json", PARENT)
    default = fixed_file(baseline / "build.json", DEFAULT_BUILD)
    fixed_file(baseline / "run-manifest.json", DEFAULT_RUN)
    observer = fixed_file(probe / "build.json", PROBE_BUILD)
    syntax = fixed_file(options.syntax_receipt, SYNTAX)
    require(syntax["exit"] == 0 and syntax["used_dependencies_unchanged"] is True, "Unsuccessful syntax cohort")
    for donor in (default, observer):
        require(donor["snapshot_manifest_sha256"] == PARENT and not donor["production_units_recompiled"]
                and donor["fork_patches_reapplied"] is False, "Incorrect production donor")
    source_guard(repo, parent)
    production_base(snapshot, repo, out, parent, create)
    return parent, default, observer, syntax

def unit(donor, basename):
    found = [row for row in donor["compiled_test_units"] if Path(row["source"]).name == basename]
    require(len(found) == 1, "Missing/ambiguous donor test unit")
    return found[0]

def compile_command(donor, name, source_root, output):
    args = list(donor["command"])
    old_source = Path(args[args.index("-c") + 1]); old_root = str(old_source).split("/native/", 1)[0]
    # Original native/Cycles flags and frozen SDK include order are preserved.
    for i, value in enumerate(args):
        if value.startswith(old_root + "/") or value.startswith("-I" + old_root + "/"):
            args[i] = value.replace(old_root, str(source_root), 1)
    args[args.index("-c") + 1] = str(source_root / "native/tests" / name)
    args[args.index("-o") + 1] = str(output)
    for flag, value in (("-MF", str(output) + ".d"), ("-MT", str(output))):
        if flag in args:
            args[args.index(flag) + 1] = value
    return args

def link_command(baseline, actor_old, actor, observer, output, dep):
    require(baseline.count(actor_old) == 1, "Ambiguous default actor replacement")
    args = [actor if value == actor_old else value for value in baseline]
    args[args.index("-o") + 1] = output
    files = [i for i, value in enumerate(args) if value.startswith("-Wl,--dependency-file=")]
    require(len(files) == 1, "Ambiguous link dependency output")
    args[files[0]] = "-Wl,--dependency-file=" + dep
    args.insert(next(i for i, value in enumerate(args) if value.endswith(".a")), observer)
    require(len(args) == len(baseline) + 1, "Unexpected link-token changes")
    return args

def headers(snapshot, folder, donor, parent, source_root):
    description = read(folder / "test-headers.json")
    require(sha(folder / "test-headers.json") == donor["test_header_manifest_sha256"], "Stale donor headers")
    integration.validate_test_headers(snapshot, folder, description)
    allowed = {str((source_root / relative).resolve()): digest
               for relative, digest in {**parent["native_source_sha256"], **SOURCES}.items()}
    for relative, digest in parent["generated_sha256"].items():
        allowed[str((snapshot / "integration-generated" / relative).resolve())] = digest
    for relative, digest in description["production_context_sha256"].items():
        allowed[str((folder / "test-headers/blender" / relative).resolve())] = digest
    for relative, info in description["headers"].items():
        allowed[str((folder / "test-headers/blender" / relative).resolve())] = info["sha256"]
    allowed.update({str(Path(path).resolve()): digest for path, digest in description["external_sha256"].items()})
    return allowed

def closure(path, snapshot, allowed):
    consumed = {}
    for name in integration.make_dependencies(path):
        name = integration.dependency_path(str(name), snapshot)
        digest = sha(name)
        require(allowed.get(str(name.resolve())) == digest, "Unadmitted consumed dependency: " + str(name))
        consumed[str(name)] = digest
    return consumed

def call(args, cwd, env, log):
    with log.open("w") as output:
        result = subprocess.run(["nice", "-n", "19", *args], cwd=cwd, env=env, stdout=output, stderr=subprocess.STDOUT)
    require(result.returncode == 0, "Command exited " + str(result.returncode) + ": " + str(log))
    return {"command": args, "returncode": result.returncode, "log": record(log)}

def build(options, repo, out):
    require(not out.exists(), "Build output must be fresh")
    out.mkdir(parents=True); started = time.monotonic()
    result = {"kind": PROFILE + "-build-result", "returncode": 1}
    try:
        parent, default, observer, syntax = parents(options, repo, out, True)
        snapshot = options.snapshot.resolve(); source = out / "test-source"
        shutil.copytree(out / "production-base/native", source / "native")
        for relative in SOURCES:
            target = source / relative; target.parent.mkdir(parents=True, exist_ok=True); shutil.copyfile(repo / relative, target)
        (out / "objects").mkdir(); (out / "bin").mkdir(); (out / "tmp").mkdir()
        env = dict(os.environ, CCACHE_DIR=str(repo.parent / "build/ccache"), TMPDIR=str(out / "tmp"))
        donors = {"actor": default, "probe": observer}
        folders = {"actor": options.baseline.resolve(), "probe": options.probe_donor.resolve()}
        commands, admitted, scans = {}, {}, {}
        for label, (old, new) in PAIRS.items():
            commands[label] = compile_command(unit(donors[label], old), new, source, out / "objects" / (label + ".o"))
            admitted[label] = headers(snapshot, folders[label], donors[label], parent, source)
        def scan(label):
            dep = out / (label + ".preprocess.d")
            checked = call(integration.dependency_scan(commands[label], dep), snapshot, env, out / (label + ".preprocess.log"))
            checked["dependencies"] = closure(dep, snapshot, admitted[label]); return label, checked
        with concurrent.futures.ThreadPoolExecutor(max_workers=options.jobs) as pool:
            scans = dict(pool.map(scan, PAIRS))
        def compile_one(label):
            checked = call(commands[label], snapshot, env, out / (label + ".compile.log"))
            obj = out / "objects" / (label + ".o")
            checked["object"] = record(obj)
            checked["dependency_sha256"] = sha(str(obj) + ".d")
            checked["dependencies"] = closure(Path(str(obj) + ".d"), snapshot, admitted[label]); return label, checked
        with concurrent.futures.ThreadPoolExecutor(max_workers=options.jobs) as pool:
            compiled = dict(pool.map(compile_one, PAIRS))
        old_actor = unit(default, PAIRS["actor"][0])["object"]
        original = next(row for row in default["links"] if Path(row["binary"]).name == "cycles-live-artist")
        main = unit(default, "cycles_live_udim_artist_main.cc")
        require(sha(main["object"]) == main["object_sha256"], "Stale retained main")
        args = link_command(original["command"], old_actor, compiled["actor"]["object"]["path"],
                            compiled["probe"]["object"]["path"], str(out / "bin/cycles-live-artist"), str(out / "integrated-link.d"))
        linked = call(args, snapshot, env, out / "link.log")
        linked["binary"] = record(out / "bin/cycles-live-artist")
        (out / "bin/lib").symlink_to(snapshot / "bin/lib", target_is_directory=True)
        for path in (snapshot / "bin").iterdir():
            if path.is_dir() and path.name[0].isdigit(): (out / "bin" / path.name).symlink_to(path, target_is_directory=True)
        parents(options, repo, out, False)
        for rows in (scans, compiled):
            for row in rows.values():
                for path, digest in row["dependencies"].items(): require(sha(path) == digest, "Dependency changed during build")
        receipt = {"schema": 1, "kind": "full37-base-with-observation-only-test-cohort", "profile": PROFILE,
                   "base_view_scope": "Exact original production/native/fork only; excludes new observer sources",
                   "parents": [record(options.snapshot / "integrated-snapshot.json"), record(options.baseline / "build.json"),
                               record(options.baseline / "run-manifest.json"), record(options.probe_donor / "build.json"), record(options.syntax_receipt)],
                   "sources": SOURCES, "writer": record(Path(__file__)), "scans": scans, "compiled_test_units": compiled,
                   "retained_main": main, "original_link": original, "link": linked,
                   "production_units_recompiled": [], "fork_patches_reapplied": False}
        (out / "build.json").write_text(json.dumps(receipt, indent=2) + "\n")
        manifest = {"schema": 1, "profile": PROFILE, "kind": "readonly-default-completion-test-cohort",
                    "parents": [record(out / "build.json")], "binaries": {linked["binary"]["path"]: linked["binary"]["sha256"]}}
        (out / "run-manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
        result.update(returncode=0, build=record(out / "build.json"), run_manifest=record(out / "run-manifest.json"), binary=linked["binary"])
    except Exception as error:
        result["error"] = str(error)
        raise
    finally:
        result["elapsed_seconds"] = time.monotonic() - started
        (out / "build-result.json").write_text(json.dumps(result, indent=2) + "\n")
        print(json.dumps(result, indent=2), flush=True)

def admit(options, repo, out):
    parent, default, observer, syntax = parents(options, repo, out, False)
    receipt, manifest = read(out / "build.json"), read(out / "run-manifest.json")
    require(receipt["profile"] == manifest["profile"] == PROFILE, "Incorrect observer profile")
    require(receipt["sources"] == SOURCES and receipt["production_units_recompiled"] == [] and receipt["fork_patches_reapplied"] is False, "Unexpected source/production refresh")
    require(receipt["writer"] == record(Path(__file__)), "Changed observer validator/build writer")
    expected_parents = [record(options.snapshot / "integrated-snapshot.json"), record(options.baseline / "build.json"),
                        record(options.baseline / "run-manifest.json"), record(options.probe_donor / "build.json"), record(options.syntax_receipt)]
    require(receipt["parents"] == expected_parents and manifest["parents"] == [record(out / "build.json")], "Incorrect cohort parents")
    require(set(receipt["compiled_test_units"]) == set(receipt["scans"]) == set(PAIRS), "Expected exactly two test TUs")
    donors = {"actor": default, "probe": observer}; folders = {"actor": options.baseline, "probe": options.probe_donor}
    for label, (old, new) in PAIRS.items():
        row = receipt["compiled_test_units"][label]; obj = out / "objects" / (label + ".o")
        require(row["object"] == record(obj) and row["returncode"] == 0, "Stale/failed observer object")
        expected = compile_command(unit(donors[label], old), new, out / "test-source", obj)
        require(row["command"] == expected, "Changed exact donor compilation flags")
        allowed = headers(options.snapshot, folders[label], donors[label], parent, out / "test-source")
        require(row["dependencies"] == closure(Path(str(obj) + ".d"), options.snapshot, allowed), "Changed compiled dependency closure")
        scan = receipt["scans"][label]
        require(scan["returncode"] == 0 and scan["command"] == integration.dependency_scan(expected, out / (label + ".preprocess.d")), "Changed preprocessing command")
        require(scan["dependencies"] == closure(out / (label + ".preprocess.d"), options.snapshot, allowed), "Changed preprocessing closure")
    main = unit(default, "cycles_live_udim_artist_main.cc")
    require(receipt["retained_main"] == main and sha(main["object"]) == main["object_sha256"], "Changed original main")
    original = next(row for row in default["links"] if Path(row["binary"]).name == "cycles-live-artist")
    args = link_command(original["command"], unit(default, PAIRS["actor"][0])["object"],
                        str((out / "objects/actor.o").resolve()), str((out / "objects/probe.o").resolve()),
                        str(out / "bin/cycles-live-artist"), str(out / "integrated-link.d"))
    require(receipt["original_link"] == original and receipt["link"]["command"] == args and receipt["link"]["returncode"] == 0, "Changed production/link substitution")
    binary = record(out / "bin/cycles-live-artist")
    require(receipt["link"]["binary"] == binary and manifest["binaries"] == {binary["path"]: binary["sha256"]}, "Stale observer binary")
    return manifest

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--profile", required=True, choices=[PROFILE])
    for name in ("snapshot", "baseline", "probe-donor", "syntax-receipt", "output"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--jobs", type=int, choices=(1, 2), default=2)
    modes = parser.add_mutually_exclusive_group(required=True)
    modes.add_argument("--build-only", action="store_true")
    modes.add_argument("--admit-only", action="store_true")
    options = parser.parse_args(); repo = Path(__file__).resolve().parents[2]
    for name in ("snapshot", "baseline", "probe_donor", "syntax_receipt", "output"):
        setattr(options, name, Path(os.path.abspath(getattr(options, name))))
    if options.build_only: build(options, repo, options.output)
    else:
        admitted = admit(options, repo, options.output)
        print(json.dumps({"profile": PROFILE, "admitted": True, "manifest": record(options.output / "run-manifest.json")}))

if __name__ == "__main__":
    main()
