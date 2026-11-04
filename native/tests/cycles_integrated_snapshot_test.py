"""Small synthetic snapshot admission checks; no real build, fork or GPU IO."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "scripts/host"))
import cycles_integrated_snapshot as integrated
from cycles_live_manifest import sha256


class IntegratedSnapshotTests(unittest.TestCase):
    def test_exact_immutable_source_patch_boundary_preserves_old_series(self):
        with tempfile.TemporaryDirectory() as temp:
            repo = Path(temp)
            folder = repo / "native/patches"
            folder.mkdir(parents=True)
            for name in ("0035-hosted-live-image-cache.patch", "0036-cycles-live-image-reload.patch"):
                (folder / name).write_text("reviewed base fixture")
            self.assertEqual(len(integrated.patch_series(repo)), 2)
            extension37 = folder / "0037-cycles-hosted-displacement-image-refresh.patch"
            extension37.write_text("reviewed displacement fixture")
            self.assertEqual(len(integrated.patch_series(repo)), 3)
            extension38 = folder / "0038-hosted-immutable-source-snapshots.patch"
            extension38.write_text("reviewed immutable source fixture")
            self.assertEqual(len(integrated.patch_series(repo)), 4)
            extension37.unlink()
            with self.assertRaises(ValueError):
                integrated.patch_series(repo)
            extension37.write_text("reviewed displacement fixture")
            for name in ("0037-alternative.patch", "0038-alternative.patch", "0039-unreviewed.patch"):
                wrong = folder / name
                wrong.write_text("not the reviewed extension")
                with self.assertRaises(ValueError):
                    integrated.patch_series(repo)
                wrong.unlink()
            extension40 = folder / "0040-partial-write-owned-embedded-self-cache.patch"
            extension40.write_text("reviewed owned embedded self-cache fixture")
            self.assertEqual(len(integrated.patch_series(repo)), 5)
            extension38.unlink()
            with self.assertRaises(ValueError):
                integrated.patch_series(repo)
            extension38.write_text("reviewed immutable source fixture")
            for name in ("0039-unreviewed.patch", "0040-alternative.patch", "0041-unreviewed.patch"):
                wrong = folder / name
                wrong.write_text("not the reviewed persistence extension")
                with self.assertRaises(ValueError):
                    integrated.patch_series(repo)
                wrong.unlink()

    def test_hosted_worker_extension_requires_exact_predecessors(self):
        with tempfile.TemporaryDirectory() as temp:
            repo = Path(temp)
            folder = repo / "native/patches"
            folder.mkdir(parents=True)
            names = ("0035-hosted-live-image-cache.patch", "0036-cycles-live-image-reload.patch",
                     "0037-cycles-hosted-displacement-image-refresh.patch",
                     "0038-hosted-immutable-source-snapshots.patch",
                     "0040-partial-write-owned-embedded-self-cache.patch",
                     "0041-hosted-shader-worker-contexts.patch")
            for name in names:
                (folder / name).write_text("synthetic reviewed patch")
            self.assertEqual(len(integrated.patch_series(repo)), 6)
            for name in names[2:5]:
                predecessor = folder / name
                predecessor.unlink()
                with self.assertRaises(ValueError):
                    integrated.patch_series(repo)
                predecessor.write_text("synthetic reviewed patch")
            for name in ("0039-unreviewed.patch", "0041-alternative.patch", "0042-unreviewed.patch"):
                unknown = folder / name
                unknown.write_text("not a reviewed extension")
                with self.assertRaises(ValueError):
                    integrated.patch_series(repo)
                unknown.unlink()

    def test_hosted_program_extension_requires_exact_predecessors(self):
        with tempfile.TemporaryDirectory() as temp:
            repo = Path(temp)
            folder = repo / "native/patches"
            folder.mkdir(parents=True)
            names = ("0035-hosted-live-image-cache.patch", "0036-cycles-live-image-reload.patch",
                     "0037-cycles-hosted-displacement-image-refresh.patch",
                     "0038-hosted-immutable-source-snapshots.patch",
                     "0040-partial-write-owned-embedded-self-cache.patch",
                     "0041-hosted-shader-worker-contexts.patch",
                     "0042-hosted-stack-program-warmup.patch")
            for name in names:
                (folder / name).write_text("synthetic reviewed patch")
            self.assertEqual(len(integrated.patch_series(repo)), 7)
            for name in names[2:6]:
                predecessor = folder / name
                predecessor.unlink()
                with self.assertRaises(ValueError):
                    integrated.patch_series(repo)
                predecessor.write_text("synthetic reviewed patch")
            for name in ("0039-unreviewed.patch", "0041-alternative.patch", "0042-alternative.patch", "0043-unreviewed.patch"):
                unknown = folder / name
                unknown.write_text("not a reviewed extension")
                with self.assertRaises(ValueError):
                    integrated.patch_series(repo)
                unknown.unlink()

    def fixture(self, root, metadata=False, cycles=False):
        repo, snapshot, build, fork = [root / name for name in ("repo", "snapshot", "artist", "blender")]
        for folder in (repo, snapshot, build, fork):
            folder.mkdir()
        def write(path, text):
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(text)
            return path
        sources = ["host/main.cc", "host/NativeCyclesImages.cc", "host/Example.hh", "host/Example.inl",
                   "tests/cycles_live_artist_test.cc", "tests/cycles_live_udim_artist_main.cc",
                   "tests/cycles_live_udim_artist_test.cc", "tests/cycles_image_cache_test.cc"]
        for name in sources:
            write(repo / "native" / name, "native source " + name)
        write(repo / "scripts/host/verify-gpu-only-paths.py", "print('source guard')\n")
        for name in ("cycles_integrated_snapshot.py", "cycles_live_manifest.py", "verify-cycles-live.py", "capture-cycles-snapshot.py"):
            write(repo / "scripts/host" / name, "synthetic runner source " + name)
        for name in ("0035-hosted-live-image-cache.patch", "0036-cycles-live-image-reload.patch"):
            write(repo / "native/patches" / name, "--- a/include/live.hh\n+++ b/include/live.hh\n@@ -1 +1 @@\n-old\n+new\n")
        write(fork / "include/live.hh", "reviewed final header")
        write(build / "generated.hh", "generated header")
        sdk = write(root / "sdk/libQt6Core.so.6.8.3", "immutable absolute SDK")
        main, bridge, archive = "objects/main.cc.o", "objects/NativeCyclesImages.cc.o", "lib/libproduction.a"
        for name in (main, bridge, archive):
            write(build / name, "completed production " + name)
            write(snapshot / name, "completed production " + name)
        commands = [f"c++ -I{repo}/native/host -I{fork}/include -I{build} -DNDEBUG -o {obj} -c {repo}/native/host/{name}"
                    for obj, name in ((main, "main.cc"), (bridge, "NativeCyclesImages.cc"))]
        if cycles:
            commands.append(f"c++ -DCYCLES_EXACT_ABI=1 -DNDEBUG -I{fork}/include -isystem {fork}/lib/linux_x64/embree/include -I{build} -o cycles/session.cpp.o -c {fork}/intern/cycles/blender/session.cpp")
        metadata_commands = []
        metadata_object = ""
        if metadata:
            write(fork / "build_files/cmake/buildinfo.cmake", "pinned timestamp generator")
            write(fork / "source/creator/buildinfo.c", "pinned metadata source")
            write(fork / "source/creator/symbols_unix.map", "pinned linker map")
            metadata_object = "objects/buildinfo.c.o"
            write(build / metadata_object, "completed metadata object")
            write(snapshot / metadata_object, "completed metadata object")
            metadata_commands = [f"cd {build} && cmake -DSOURCE_DIR={fork} -DBUILD_DATE= -DBUILD_TIME= -P {fork}/build_files/cmake/buildinfo.cmake",
                                 f"cc -DNDEBUG -o {metadata_object} -c {fork}/source/creator/buildinfo.c"]
            commands += metadata_commands
        script = f"-Wl,--version-script={fork}/source/creator/symbols_unix.map" if metadata else ""
        commands += [f": && c++ {script} -o bin/bts-host {main} {bridge} {metadata_object} {archive} {sdk} && :"]
        write(snapshot / "host-commands.json", json.dumps(commands))
        deps = "".join(f"{obj}: #deps 4, deps mtime 1 (VALID)\n    {repo}/native/host/{name}\n    {fork}/include/live.hh\n    {build}/generated.hh\n    {repo}/native/host/Example.inl\n" for obj, name in ((main, "main.cc"), (bridge, "NativeCyclesImages.cc")))
        write(snapshot / "native-deps.txt", deps)
        write(snapshot / "snapshot.json", json.dumps({"source_head": "tested-integration", "source_build": str(build)}))
        write(snapshot / "bin/5.2/resource.txt", "runtime")
        log = write(root / "confirmation.log", "Actual recorded command completed successfully\n")
        result = write(root / "confirmation.json", json.dumps({"returncode": 0, "source_head": "tested-integration", "command": ["cmake", "--build", str(build), "--target", "bts-host", "bts-cpu-tests"], "log": str(log)}))
        cpu = write(root / "results.json", json.dumps([{"test": "example", "returncode": 0}]))
        receipt = write(root / "fork-receipt.json", json.dumps({"schema": 1, "pin": integrated.PIN, "source_head": "tested-integration", "patch_sha256": integrated.patch_series(repo), "actual_fork_sha256": {"include/live.hh": sha256(fork / "include/live.hh")}, "exact_reviewed_series_equal": True, "read_only_actual_checkout": True}))
        def git(args, **kwargs):
            return integrated.PIN + "\n" if args[-2:] == ["rev-parse", "HEAD"] else "include/live.hh\n"
        def run(args, **kwargs):
            if "ninja-build" in args:
                pending = [f"cd {build} && python3 {repo}/scripts/host/verify-gpu-only-paths.py"]
                if metadata:
                    pending += [*metadata_commands, commands[-1]]
                text = "".join(f"[{i}/{len(pending)}] {command}\n" for i, command in enumerate(pending, 1))
                return SimpleNamespace(stdout=f"ninja: Entering directory `{build}'\n" + text, stderr="", returncode=0)
            self.assertEqual(Path(args[-1]), repo / "scripts/host/verify-gpu-only-paths.py")
            kwargs["stdout"].write("GPU source guard PASS\n")
            return SimpleNamespace(returncode=0)
        with patch.object(integrated.subprocess, "check_output", side_effect=git), patch.object(integrated.subprocess, "run", side_effect=run):
            integrated.capture(snapshot, repo, fork, result, cpu, receipt)
        return repo, snapshot, build, fork, sdk

    def test_rejects_stale_incomplete_or_wrong_integration(self):
        with tempfile.TemporaryDirectory() as directory:
            repo, snapshot, build, fork, sdk = self.fixture(Path(directory))
            manifest = snapshot / "integrated-snapshot.json"
            original = manifest.read_text()
            integrated.validate(snapshot, repo)
            for target in (repo / "native/host/Example.hh", repo / "native/host/Example.inl", snapshot / "lib/libproduction.a", sdk,
                           snapshot / "integration-generated/generated.hh", snapshot / "integration-blender/include/live.hh"):
                saved = target.read_bytes()
                target.write_bytes(saved + b" changed")
                with self.assertRaises(ValueError):
                    integrated.validate(snapshot, repo)
                target.write_bytes(saved)
            for mutation in ("source", "header", "input", "receipt", "compile"):
                m = json.loads(original)
                if mutation == "source":
                    del m["native_source_sha256"]["native/host/Example.hh"]
                elif mutation == "header":
                    del m["blender_source_sha256"]["include/live.hh"]
                elif mutation == "input":
                    del m["link_inputs"]["lib/libproduction.a"]
                elif mutation == "receipt":
                    m["blender_source_sha256"]["include/live.hh"] = "0" * 64
                else:
                    log = snapshot / "integration-evidence/ninja-dry-run.log"
                    saved = log.read_text()
                    log.write_text("[1/1] c++ -c unrebuilt-consumer.cc\n")
                    m["evidence_sha256"]["integration-evidence/ninja-dry-run.log"] = sha256(log)
                manifest.write_text(json.dumps(m))
                with self.assertRaises(ValueError):
                    integrated.validate(snapshot, repo)
                if mutation == "compile":
                    log.write_text(saved)
            manifest.write_text(original)
            integrated.validate(snapshot, repo)
            for text in ("[1/1] ar qc lib.a object.o\n", "[1/1] cmake --install .\n", "[1/1] python3 other-guard.py\n"):
                with self.assertRaises(ValueError):
                    integrated.admitted_dry_run(text, repo, build)
            self.assertTrue(integrated.is_link_input(str(sdk)))

    def test_exact_timestamp_metadata_chain_is_not_a_native_rebuild(self):
        with tempfile.TemporaryDirectory() as directory:
            repo, snapshot, build, fork, sdk = self.fixture(Path(directory), metadata=True)
            m = integrated.validate(snapshot, repo)
            self.assertEqual({entry["kind"] for entry in m["pending_commands"]},
                             {"gpu-source-guard", "buildinfo-generator", "buildinfo-object", "buildinfo-link"})
            self.assertIn("objects/buildinfo.c.o", m["link_inputs"])
            commands = json.loads((snapshot / "host-commands.json").read_text())
            log = (snapshot / "integration-evidence/ninja-dry-run.log").read_text()
            for changed in (log.replace("-DNDEBUG -o objects/buildinfo.c.o", "-O0 -o objects/buildinfo.c.o"),
                            log.replace("source/creator/buildinfo.c\n", "source/creator/other.c\n"),
                            log.replace(" -o bin/bts-host ", " -o bin/other-host "),
                            "\n".join(line for line in log.splitlines() if "buildinfo.c.o -c" not in line)):
                with self.assertRaises(ValueError):
                    integrated.admitted_dry_run(changed, repo, build, commands, fork)
            source = snapshot / "integration-blender/source/creator/symbols_unix.map"
            source.write_text("changed linker script")
            with self.assertRaises(ValueError):
                integrated.validate(snapshot, repo)

    def test_integrated_builder_only_compiles_test_drivers(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            repo, snapshot, build, fork, sdk = self.fixture(root, metadata=True)
            out = root / "proof"
            out.mkdir()
            production = {p: p.read_bytes() for p in (snapshot / "objects").glob("*")}
            production[snapshot / "lib/libproduction.a"] = (snapshot / "lib/libproduction.a").read_bytes()
            calls = []
            def run(args, **kwargs):
                calls.append(args)
                self.assertEqual(args[:3], ["nice", "-n", "19"])
                self.assertEqual(args[3], "c++")
                command = args[3:]
                if "-M" in command:
                    source = next(value for value in command if value.endswith(".cc"))
                    Path(command[command.index("-MF") + 1]).write_text("headers: " + source + "\n")
                    return SimpleNamespace(returncode=0)
                elif "-c" in command:
                    self.assertTrue(Path(command[command.index("-c") + 1]).is_relative_to(snapshot / "integration-source/native/tests"))
                else:
                    self.assertNotIn(str(build / "objects/NativeCyclesImages.cc.o"), command)
                    self.assertIn(str(snapshot / "objects/NativeCyclesImages.cc.o"), command)
                    self.assertIn(str(sdk), command)
                    self.assertIn("-Wl,--version-script=" + str(snapshot / "integration-blender/source/creator/symbols_unix.map"), command)
                Path(command[command.index("-o") + 1]).write_bytes(b"test compiler output")
                return SimpleNamespace(returncode=0)
            with patch.object(integrated.subprocess, "run", side_effect=run):
                record = integrated.build_drivers(snapshot, repo, out, True, {})
            self.assertEqual(len(calls), 11)  # Six preprocess scans, three test TUs, two links.
            self.assertFalse(record["fork_patches_reapplied"])
            self.assertEqual(record["production_units_recompiled"], [])
            for path, value in production.items():
                self.assertEqual(path.read_bytes(), value)
            # A file edited while the old loaded helper is still executing
            # must never attribute that binary to the new on-disk helper.
            def changed_runner(args, **kwargs):
                result = run(args, **kwargs)
                if "-M" not in args and "-c" not in args:
                    (repo / "scripts/host/verify-cycles-live.py").write_text("changed during build")
                return result
            second = root / "changed-proof"
            second.mkdir()
            with patch.object(integrated.subprocess, "run", side_effect=changed_runner), self.assertRaisesRegex(ValueError, "Runner source changed"):
                integrated.build_drivers(snapshot, repo, second, True, {})

    def test_test_header_supplement_requires_pin_and_retains_sdk_identity(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            repo, snapshot, build, fork, sdk = self.fixture(root)
            admitted = integrated.validate(snapshot, repo)
            extra = fork / "include/test-only.hh"
            extra.write_bytes(b"pinned test initialization header")
            out = root / "test-headers-proof"
            out.mkdir()
            source = snapshot / "integration-source/native/tests/cycles_image_cache_test.cc"
            calls = []
            def scan(args, label):
                calls.append(args)
                Path(args[args.index("-MF") + 1]).write_text(f"headers: {source} {extra} {sdk}\n")
            commands = {"cache.cc": ["c++", "-c", str(source), "-o", "unused.o"]}
            with patch.object(integrated.subprocess, "check_output", return_value=extra.read_bytes()):
                record = integrated.freeze_test_headers(snapshot, out, admitted, commands, scan)
            self.assertEqual(len(calls), 1)
            self.assertEqual(record["headers"]["include/test-only.hh"]["kind"], "pinned-git-show")
            integrated.validate_test_headers(snapshot, out, record)
            context = out / "test-headers/blender/include/live.hh"
            original_context = context.read_bytes()
            self.assertEqual(original_context, (snapshot / "integration-blender/include/live.hh").read_bytes())
            context.write_bytes(b"changed frozen relative-include context")
            with self.assertRaisesRegex(ValueError, "Stale frozen production"):
                integrated.validate_test_headers(snapshot, out, record)
            context.write_bytes(original_context)
            incomplete = dict(record, production_context_sha256={})
            with self.assertRaisesRegex(ValueError, "Incomplete frozen production"):
                integrated.validate_test_headers(snapshot, out, incomplete)
            frozen = out / "test-headers/blender/include/test-only.hh"
            frozen.write_bytes(b"changed")
            with self.assertRaisesRegex(ValueError, "Stale supplemental"):
                integrated.validate_test_headers(snapshot, out, record)
            with patch.object(integrated.subprocess, "check_output", return_value=b"different pinned bytes"), self.assertRaisesRegex(ValueError, "differs from pin"):
                integrated.freeze_test_headers(snapshot, out, admitted, commands, scan)
            sdk.write_bytes(b"changed SDK")
            frozen.write_bytes(extra.read_bytes())
            with self.assertRaisesRegex(ValueError, "Stale test compiler/SDK"):
                integrated.validate_test_headers(snapshot, out, record)

    def test_sdk_submodule_header_requires_clean_parent_gitlink_and_exact_blob(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            repo, snapshot, build, fork, sdk = self.fixture(root)
            admitted = integrated.validate(snapshot, repo)
            relative = "lib/linux_x64/include/sdk.hh"
            extra = fork / relative
            extra.parent.mkdir(parents=True)
            extra.write_text("actual pinned SDK header")
            content, pin, blob = extra.read_bytes(), "1" * 40, "2" * 40
            out = root / "sdk-test-headers"
            out.mkdir()
            source = snapshot / "integration-source/native/tests/cycles_image_cache_test.cc"
            def scan(args, label):
                Path(args[args.index("-MF") + 1]).write_text(f"headers: {source} {extra}\n")
            def git(args, **kwargs):
                if "ls-tree" in args: return f"160000 commit {pin}\tlib/linux_x64\n"
                if "status" in args: return ""
                if args[-1] == "HEAD": return pin + "\n"
                if "rev-parse" in args: return blob + "\n"
                return content
            commands = {"observer.cc": ["c++", "-c", str(source), "-o", "unused.o"]}
            with patch.object(integrated.subprocess, "check_output", side_effect=git):
                record = integrated.freeze_test_headers(snapshot, out, admitted, commands, scan)
                self.assertEqual(record["headers"][relative], {"kind":"pinned-gitlink", "pin":integrated.PIN, "submodule":"lib/linux_x64", "submodule_pin":pin, "submodule_path":"include/sdk.hh", "blob":blob, "sha256":sha256(extra)})
                extra.write_bytes(b"changed actual SDK header")
                with self.assertRaisesRegex(ValueError, "differs from pin"):
                    integrated.freeze_test_headers(snapshot, out, admitted, commands, scan)
            for failure in ("parent", "head", "dirty", "lfs"):
                def broken(args, **kwargs):
                    if failure == "parent" and "ls-tree" in args: return "100644 blob " + blob + "\tlib/linux_x64\n"
                    if failure == "head" and args[-1] == "HEAD": return "3" * 40
                    if failure == "dirty" and "status" in args: return " M include/sdk.hh\n"
                    if failure == "lfs" and "show" in args: return b"version https://git-lfs.github.com/spec/v1\n"
                    return git(args, **kwargs)
                with patch.object(integrated.subprocess, "check_output", side_effect=broken), self.assertRaises(ValueError, msg=failure):
                    integrated.pinned_test_header(fork, relative, {})

    def test_explicit_controlled_overlay_preserves_production_and_cycles_flags(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            repo, snapshot, build, fork, sdk = self.fixture(root, cycles=True)
            def run(args, **kwargs):
                command = args[3:]
                if "-M" in command:
                    path = next(value for value in command if value.endswith(".cc"))
                    Path(command[command.index("-MF") + 1]).write_text("test: " + path + "\n")
                else:
                    Path(command[command.index("-o") + 1]).write_bytes(b"unchanged default test artifact")
                return SimpleNamespace(returncode=0)
            baseline = root / "baseline"
            baseline.mkdir()
            with patch.object(integrated.subprocess, "run", side_effect=run):
                integrated.build_drivers(snapshot, repo, baseline, True, {})
            for relative in integrated.CONTROLLED_TEST_SOURCES:
                path = repo / relative
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text("explicit controlled test overlay " + relative)
            overlay = integrated.controlled_overlay(snapshot, repo)
            admitted = integrated.validate(snapshot, repo, overlay)
            for bad in ({}, {key: value for key, value in overlay.items() if not key.endswith(".hh")},
                        {**overlay, "native/host/main.cc": {"sha256": sha256(repo / "native/host/main.cc")}}):
                with self.assertRaises(ValueError):
                    integrated.validate(snapshot, repo, bad)
            output = root / "controlled"
            output.mkdir()
            original_inputs = {p: p.read_bytes() for p in (snapshot / "objects").glob("*")}
            with patch.object(integrated.subprocess, "run", side_effect=run):
                record = integrated.build_drivers(snapshot, repo, output, True, {}, True, baseline / "run-manifest.json")
            self.assertEqual(record["profile"], integrated.CONTROLLED_PROFILE)
            self.assertEqual(len(record["compiled_test_units"]), 3)
            observer = next(unit for unit in record["compiled_test_units"] if unit["source"].endswith("CyclesLiveCompletionProbe.cc"))
            self.assertIn("-DCYCLES_EXACT_ABI=1", observer["command"])
            self.assertNotIn("-g0", observer["command"])
            self.assertNotIn("-DBTS_BA_CONTROLLED_PROFILE_TEST=1", observer["command"])
            system = observer["command"].index("-isystem")
            self.assertEqual(observer["command"][system + 1], str(output / "test-headers/blender/lib/linux_x64/embree/include"))
            self.assertIn("-I" + str(output / "test-headers/blender/include"), observer["command"])
            self.assertTrue(all("-DBTS_BA_CONTROLLED_PROFILE_TEST=1" in unit["command"] for unit in record["compiled_test_units"] if unit is not observer))
            self.assertEqual(record["production_units_recompiled"], [])
            self.assertEqual(record["links"][1]["retained_from_default_parent"], str(baseline / "bin/cycles-cache-cpu"))
            integrated.validate_overlay_copy(output, admitted, overlay)
            for path, content in original_inputs.items():
                self.assertEqual(path.read_bytes(), content)
            stale = output / "controlled-source/native/host/Example.hh"
            stale.write_text("changed copied production header")
            with self.assertRaisesRegex(ValueError, "Stale controlled source"):
                integrated.validate_overlay_copy(output, admitted, overlay)
            original = (repo / "native/host/Example.hh").read_bytes()
            (repo / "native/host/Example.hh").write_bytes(original + b"changed")
            with self.assertRaisesRegex(ValueError, "Current native source differs"):
                integrated.validate(snapshot, repo, overlay)

    def test_digest_memo_rejects_replacement_and_restored_mtime(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "input.a"
            path.write_bytes(b"old bytes")
            original = path.stat()
            with patch.object(integrated, "_sha256", wraps=sha256) as reader:
                first = integrated.sha256(path)
                self.assertEqual(integrated.sha256(path), first)
                self.assertEqual(reader.call_count, 1 if os.name == "posix" else 2)
                path.write_bytes(b"new bytes")
                os.utime(path, ns=(original.st_atime_ns, original.st_mtime_ns))
                second = integrated.sha256(path)
                self.assertNotEqual(second, first)
                replacement = path.with_suffix(".replacement")
                replacement.write_bytes(b"old bytes")
                os.utime(replacement, ns=(original.st_atime_ns, original.st_mtime_ns))
                replacement.replace(path)
                self.assertEqual(integrated.sha256(path), first)
                self.assertEqual(reader.call_count, 3 if os.name == "posix" else 4)


if __name__ == "__main__":
    unittest.main()
