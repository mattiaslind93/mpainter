# SPDX-License-Identifier: GPL-3.0-or-later
"""Fixture tests for scripts/verify/format_owned_manifest.py.

Every test builds its own git repository in a temporary folder. Nothing here
reads or writes the real mPainter tree.
"""

import hashlib
import importlib.util
import json
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

HERE = Path(__file__).resolve().parent
SPEC = importlib.util.spec_from_file_location("format_owned_manifest", HERE.parent / "format_owned_manifest.py")
fom = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(fom)

GIT_ENV = {
    "GIT_CONFIG_GLOBAL": os.devnull,
    "GIT_CONFIG_NOSYSTEM": "1",
    "GIT_AUTHOR_NAME": "fixture",
    "GIT_AUTHOR_EMAIL": "fixture@invalid",
    "GIT_COMMITTER_NAME": "fixture",
    "GIT_COMMITTER_EMAIL": "fixture@invalid",
    "GIT_AUTHOR_DATE": "2026-01-01T00:00:00Z",
    "GIT_COMMITTER_DATE": "2026-01-01T00:00:00Z",
}


def fixture_git(root, *args, data=None):
    proc = subprocess.run(["git", "-C", str(root), *args], input=data, capture_output=True,
                          env=dict(os.environ, **GIT_ENV), timeout=60, check=True)
    return proc.stdout


class Repo:
    def __init__(self, test):
        self._tmp = tempfile.TemporaryDirectory()
        test.addCleanup(self._tmp.cleanup)
        self.outside = Path(self._tmp.name) / "outside"
        self.outside.mkdir()
        self.root = Path(self._tmp.name) / "repo"
        self.root.mkdir()
        fixture_git(self.root, "init", "-q", "-b", "main")
        (self.root / ".night-run").mkdir()

    def write(self, rel, data=b"int x;\n", mode=0o644):
        path = self.root / rel
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
        path.chmod(mode)
        return path

    def commit(self, *paths):
        fixture_git(self.root, "add", "--", *(paths or ["."]))
        fixture_git(self.root, "-c", "commit.gpgsign=false", "commit", "-q", "-m", "fixture")

    def manifest(self, **kwargs):
        return fom.build(self.root, "HEAD", **kwargs)

    def entry(self, manifest, path):
        found = [e for e in manifest["files"] if e["path"] == path]
        self.assert_one(found, path)
        return found[0]

    @staticmethod
    def assert_one(found, path):
        if len(found) != 1:
            raise AssertionError(f"{path!r}: {len(found)} entries")


def statuses(manifest):
    return {e["path"]: e["status"] for e in manifest["files"]}


class TrackedOnly(unittest.TestCase):
    def test_untracked_and_ignored_files_are_never_listed(self):
        repo = Repo(self)
        repo.write("native/host/Owned.cc")
        repo.write(".gitignore", b"/build/\n")
        repo.commit()
        repo.write("native/host/Untracked.cc")
        repo.write("build/Generated.cc")
        repo.write(".format-pilot-before.cc")
        self.assertEqual(statuses(repo.manifest()), {"native/host/Owned.cc": "include"})

    def test_staged_but_uncommitted_file_is_not_at_base(self):
        repo = Repo(self)
        repo.write("native/host/A.cc")
        repo.commit()
        repo.write("native/host/B.cc")
        fixture_git(repo.root, "add", "--", "native/host/B.cc")
        self.assertNotIn("native/host/B.cc", statuses(repo.manifest()))

    def test_fragments_are_listed_as_review_never_included(self):
        repo = Repo(self)
        repo.write("native/host/State.inc")
        repo.write("native/tests/cases.inc")
        repo.commit()
        manifest = repo.manifest()
        self.assertEqual(statuses(manifest), {"native/host/State.inc": "review", "native/tests/cases.inc": "review"})
        self.assertIn("fragment", repo.entry(manifest, "native/host/State.inc")["reasons"][0])

    def test_non_source_extensions_are_skipped(self):
        repo = Repo(self)
        repo.write("native/host/CMakeLists.txt", b"project(x)\n")
        repo.write("native/host/tool.py", b"print(1)\n")
        repo.write("native/host/A.hh")
        repo.commit()
        self.assertEqual(statuses(repo.manifest()), {"native/host/A.hh": "include"})


class Ownership(unittest.TestCase):
    def test_owned_roots_and_files(self):
        repo = Repo(self)
        for rel in ("native/host/A.cc", "native/tests/a_test.cc", "native/spsm/semantic/S.hh",
                    "native/sbsar_metadata/dump.cc", "native/host/M.inl", "scripts/host/qt-probe/main.cc"):
            repo.write(rel)
        repo.commit()
        manifest = repo.manifest()
        self.assertTrue(all(s == "include" for s in statuses(manifest).values()), statuses(manifest))
        self.assertEqual(repo.entry(manifest, "scripts/host/qt-probe/main.cc")["owner"], "scripts-probe")

    def test_unknown_ownership_is_review_not_include(self):
        repo = Repo(self)
        repo.write("src/Other.cc")
        repo.write("scripts/host/other-probe/main.cc")
        repo.write("native/newlib/X.cc")
        repo.commit()
        manifest = repo.manifest()
        for path in ("src/Other.cc", "scripts/host/other-probe/main.cc", "native/newlib/X.cc"):
            entry = repo.entry(manifest, path)
            self.assertEqual(entry["status"], "review")
            self.assertIn("ownership unknown", entry["reasons"][0])

    def test_platform_specific_is_excluded(self):
        repo = Repo(self)
        repo.write("scripts/cross-windows/probe.cc")
        repo.write("native/host/MetalView.mm")
        repo.write("native/host/macos/Bridge.cc")
        repo.commit()
        self.assertEqual(set(statuses(repo.manifest()).values()), {"exclude"})


class ForeignCode(unittest.TestCase):
    def test_vendor_generated_and_build_paths_are_excluded(self):
        repo = Repo(self)
        for rel in ("third_party/blender/x.cc", "native/host/vendor/v.hh", "native/host/generated/g.cc",
                    "native/tests/external/e.c", "native/host/build/b.cc", "native/host/_deps/d.cpp"):
            repo.write(rel)
        repo.commit()
        manifest = repo.manifest()
        self.assertEqual(set(statuses(manifest).values()), {"exclude"})
        self.assertEqual(repo.entry(manifest, "third_party/blender/x.cc")["reasons"], ["third-party path"])

    def test_generated_marker_in_header_is_review(self):
        repo = Repo(self)
        repo.write("native/host/Gen.cc", b"// Generated by tool. Do not edit.\nint x;\n")
        repo.write("native/host/Lic.hh", b"/* Copyright (c) Someone Else */\nint y;\n")
        repo.write("native/host/Spdx.cc", b"// SPDX-License-Identifier: GPL-2.0-or-later\nint y;\n")
        repo.write("native/host/Code.cc", b"int f(int c) { return g(c); } // (c) is a name here\n")
        repo.write("native/host/Ok.cc", b"int z;\n" + b" " * fom.HEADER_BYTES + b"// do not edit\n")
        repo.commit()
        manifest = repo.manifest()
        self.assertEqual(repo.entry(manifest, "native/host/Gen.cc")["status"], "review")
        self.assertEqual(repo.entry(manifest, "native/host/Lic.hh")["status"], "review")
        self.assertEqual(repo.entry(manifest, "native/host/Spdx.cc")["status"], "review")
        self.assertEqual(repo.entry(manifest, "native/host/Code.cc")["status"], "include")
        # The marker scan is bounded to the header, by design.
        self.assertEqual(repo.entry(manifest, "native/host/Ok.cc")["status"], "include")

    def test_folder_with_its_own_licence_is_review(self):
        repo = Repo(self)
        repo.write("LICENSE", b"GPL\n")
        repo.write("native/host/Own.cc")
        repo.write("native/host/openpbr/LICENSE", b"Apache\n")
        repo.write("native/host/openpbr/deep/Lib.cc")
        repo.commit()
        manifest = repo.manifest()
        self.assertEqual(repo.entry(manifest, "native/host/Own.cc")["status"], "include")
        entry = repo.entry(manifest, "native/host/openpbr/deep/Lib.cc")
        self.assertEqual(entry["status"], "review")
        self.assertIn("native/host/openpbr/LICENSE", entry["reasons"][0])

    def test_binary_content_is_excluded(self):
        repo = Repo(self)
        repo.write("native/host/Bin.cc", b"int x;\0\1\2")
        repo.commit()
        self.assertEqual(repo.entry(repo.manifest(), "native/host/Bin.cc")["status"], "exclude")

    def test_crlf_and_non_utf8_are_review(self):
        repo = Repo(self)
        repo.write(".gitattributes", b"* -text\n")
        repo.write("native/host/Crlf.cc", b"int x;\r\n")
        repo.write("native/host/Latin.cc", b"// \xe5\xe4\n")
        repo.commit()
        manifest = repo.manifest()
        self.assertEqual(repo.entry(manifest, "native/host/Crlf.cc")["status"], "review")
        self.assertEqual(repo.entry(manifest, "native/host/Latin.cc")["status"], "review")

    def test_nested_checkout_is_excluded(self):
        repo = Repo(self)
        repo.write("native/host/inner/A.cc")
        repo.commit()
        (repo.root / "native/host/inner/.git").write_text("gitdir: elsewhere\n")
        entry = repo.entry(repo.manifest(), "native/host/inner/A.cc")
        self.assertEqual(entry["status"], "exclude")
        self.assertIn("nested git checkout", entry["reasons"][0])


class LinksAndEscapes(unittest.TestCase):
    def test_symlink_in_git_is_excluded_and_never_read(self):
        repo = Repo(self)
        secret = repo.outside / "secret.cc"
        secret.write_text("int secret;\n")
        os.symlink(secret, repo.root / "native-link.cc")
        (repo.root / "native/host").mkdir(parents=True)
        os.symlink(secret, repo.root / "native/host/Link.cc")
        repo.commit()
        with mock.patch("builtins.open", wraps=open) as spy:
            entry = repo.entry(repo.manifest(), "native/host/Link.cc")
        self.assertEqual((entry["status"], entry["mode"]), ("exclude", "120000"))
        opened = [str(call.args[0]) for call in spy.call_args_list]
        self.assertFalse(any("secret" in path or "Link.cc" in path for path in opened), opened)

    def test_worktree_symlink_over_tracked_file_is_excluded(self):
        repo = Repo(self)
        repo.write("native/host/A.cc")
        repo.commit()
        target = repo.outside / "A.cc"
        target.write_bytes(b"int x;\n")
        (repo.root / "native/host/A.cc").unlink()
        os.symlink(target, repo.root / "native/host/A.cc")
        entry = repo.entry(repo.manifest(), "native/host/A.cc")
        self.assertEqual(entry["status"], "exclude")
        self.assertIn("symlink", entry["reasons"][0])

    def test_symlinked_parent_folder_escaping_root_is_excluded(self):
        repo = Repo(self)
        repo.write("native/tests/a_test.cc")
        repo.commit()
        moved = repo.outside / "tests"
        (repo.root / "native/tests").rename(moved)
        os.symlink(moved, repo.root / "native/tests")
        entry = repo.entry(repo.manifest(), "native/tests/a_test.cc")
        self.assertEqual(entry["status"], "exclude")
        self.assertIn("is a symlink", entry["reasons"][0])

    def test_hard_link_is_excluded(self):
        repo = Repo(self)
        repo.write("native/host/A.cc")
        repo.commit()
        os.link(repo.root / "native/host/A.cc", repo.outside / "copy.cc")
        self.assertEqual(repo.entry(repo.manifest(), "native/host/A.cc")["status"], "exclude")

    def test_gitlink_is_excluded(self):
        repo = Repo(self)
        repo.write("native/host/A.cc")
        repo.commit()
        head = fixture_git(repo.root, "rev-parse", "HEAD").decode().strip()
        fixture_git(repo.root, "update-index", "--add", "--cacheinfo", f"160000,{head},native/host/Sub.cc")
        fixture_git(repo.root, "-c", "commit.gpgsign=false", "commit", "-q", "-m", "gitlink")
        entry = repo.entry(repo.manifest(), "native/host/Sub.cc")
        self.assertEqual(entry["status"], "exclude")
        self.assertEqual(entry["reasons"], ["gitlink (submodule)"])

    def test_executable_mode_is_review(self):
        repo = Repo(self)
        repo.write("native/host/Exec.cc", mode=0o755)
        repo.commit()
        self.assertEqual(repo.entry(repo.manifest(), "native/host/Exec.cc")["status"], "review")


class UnusualNames(unittest.TestCase):
    def test_unusual_names_are_kept_exact_or_sent_to_review(self):
        repo = Repo(self)
        names = {
            "native/host/With Space.cc": "include",
            "native/host/Åäö.cc": "include",
            "native/host/new\nline.cc": "review",
            "native/host/tab\there.cc": "review",
            "native/host/back\\slash.cc": "review",
            "native/host/-dash.cc": "review",
            "native/host/Upper.CC": "review",
        }
        for name in names:
            repo.write(name)
        repo.commit()
        self.assertEqual(statuses(repo.manifest()), names)

    def test_non_utf8_path_is_review(self):
        repo = Repo(self)
        raw = os.fsencode(str(repo.root)) + b"/native/host/bad\xff.cc"
        os.makedirs(os.path.dirname(raw), exist_ok=True)
        with open(raw, "wb") as handle:
            handle.write(b"int x;\n")
        repo.commit()
        entries = repo.manifest()["files"]
        self.assertEqual(len(entries), 1)
        self.assertEqual(entries[0]["status"], "review")
        self.assertEqual(entries[0]["reasons"], ["path is not UTF-8"])

    def test_path_checks_reject_escape_shapes(self):
        for raw in (b"/abs/a.cc", b"native/../a.cc", b"native//a.cc", b"./a.cc"):
            self.assertTrue(fom.path_problems(raw), raw)
        self.assertEqual(fom.path_problems("native/host/Å b.cc".encode()), [])


class Provenance(unittest.TestCase):
    def test_blob_id_matches_git(self):
        repo = Repo(self)
        repo.write("native/host/A.cc", b"int a = 1;\n")
        repo.commit()
        entry = repo.entry(repo.manifest(), "native/host/A.cc")
        self.assertEqual(entry["blob"], fixture_git(repo.root, "rev-parse", "HEAD:native/host/A.cc").decode().strip())
        self.assertEqual(entry["sha256"], hashlib.sha256(b"int a = 1;\n").hexdigest())
        self.assertEqual(fom.blob_id(b"int a = 1;\n", 40), entry["blob"])

    def test_worktree_edit_is_review(self):
        repo = Repo(self)
        repo.write("native/host/A.cc")
        repo.commit()
        repo.write("native/host/A.cc", b"int changed;\n")
        entry = repo.entry(repo.manifest(), "native/host/A.cc")
        self.assertEqual(entry["status"], "review")
        self.assertIn("differ from the base blob", entry["reasons"][0])

    def test_base_is_pinned_commit_not_head(self):
        repo = Repo(self)
        repo.write("native/host/A.cc")
        repo.commit()
        first = fixture_git(repo.root, "rev-parse", "HEAD").decode().strip()
        repo.write("native/host/B.cc")
        repo.commit()
        manifest = fom.build(repo.root, first)
        self.assertEqual(manifest["summary"]["base"], first)
        self.assertEqual(list(statuses(manifest)), ["native/host/A.cc"])

    def test_bad_base_and_option_injection_fail(self):
        repo = Repo(self)
        repo.write("native/host/A.cc")
        repo.commit()
        for base in ("does-not-exist", "--output=/tmp/x", "-h"):
            with self.assertRaises(fom.ManifestError):
                fom.build(repo.root, base)

    def test_root_must_be_worktree_top(self):
        repo = Repo(self)
        repo.write("native/host/A.cc")
        repo.commit()
        with self.assertRaises(fom.ManifestError):
            fom.build(repo.root / "native", "HEAD")


class ReadOnly(unittest.TestCase):
    def snapshot(self, root):
        result = {}
        for dirpath, dirnames, filenames in os.walk(root, followlinks=False):
            for name in dirnames + filenames:
                path = Path(dirpath) / name
                info = os.lstat(path)
                data = path.read_bytes() if path.is_file() and not path.is_symlink() else b""
                result[str(path)] = (info.st_mode, info.st_size, info.st_mtime_ns, data)
        return result

    def test_build_changes_nothing_and_only_runs_read_only_git(self):
        repo = Repo(self)
        marker = repo.outside / "ran"
        repo.write("native/host/Evil.cc", f'// $(touch {marker})\n#include "x.hh"\nint main() {{}}\n'.encode())
        repo.write("native/host/x.hh", f"`touch {marker}`\n".encode())
        repo.commit()
        before = self.snapshot(repo.root)
        calls = []
        real_popen = subprocess.Popen

        def spy(argv, *args, **kwargs):
            calls.append(list(argv))
            if argv[0] != "git":
                raise AssertionError(f"ran {argv}")
            return real_popen(argv, *args, **kwargs)

        with mock.patch.object(subprocess, "Popen", side_effect=spy), \
                mock.patch.object(os, "system", side_effect=AssertionError("os.system")), \
                mock.patch.object(os, "posix_spawn", side_effect=AssertionError("posix_spawn")), \
                mock.patch.object(os, "execv", side_effect=AssertionError("execv")):
            repo.manifest()
        self.assertEqual(self.snapshot(repo.root), before)
        self.assertFalse(marker.exists())
        self.assertTrue(calls)
        for argv in calls:
            self.assertEqual(argv[:3], ["git", "-C", str(repo.root.resolve())])
            self.assertIn(argv[3], {"rev-parse", "ls-tree"})

    def test_git_wrapper_refuses_write_commands(self):
        for command in ("add", "commit", "checkout", "reset", "config", "update-index"):
            with self.assertRaises(fom.ManifestError):
                fom.git(Path("."), command)


class Output(unittest.TestCase):
    def test_manifest_is_deterministic(self):
        repo = Repo(self)
        for rel in ("native/host/B.cc", "native/host/A.cc", "native/tests/z.cc", "third_party/x.cc"):
            repo.write(rel)
        repo.commit()
        first = fom.write_output(repo.root, ".night-run/one.json", repo.manifest())
        second = fom.write_output(repo.root, ".night-run/two.json", repo.manifest())
        self.assertEqual(first.read_bytes(), second.read_bytes())
        paths = [e["path"] for e in json.loads(first.read_text())["files"]]
        self.assertEqual(paths, sorted(paths))

    def test_existing_output_is_kept(self):
        repo = Repo(self)
        repo.write("native/host/A.cc")
        repo.commit()
        existing = repo.root / ".night-run/out.json"
        existing.write_text("keep me")
        with self.assertRaises(fom.ManifestError):
            fom.write_output(repo.root, ".night-run/out.json", repo.manifest())
        self.assertEqual(existing.read_text(), "keep me")

    def test_output_outside_root_or_through_symlink_is_refused(self):
        repo = Repo(self)
        repo.write("native/host/A.cc")
        repo.commit()
        manifest = repo.manifest()
        os.symlink(repo.outside, repo.root / "escape")
        for target in (str(repo.outside / "out.json"), "escape/out.json", "../outside/out.json"):
            with self.assertRaises(fom.ManifestError, msg=target):
                fom.write_output(repo.root, target, manifest)
        self.assertEqual(list(repo.outside.iterdir()), [])

    def test_write_scope_marks_included_files(self):
        repo = Repo(self)
        repo.write("native/host/A.cc")
        repo.write("native/host/B.cc")
        repo.commit()
        scope = {"tracked_write_candidates": ["native/host/A.cc", "native/host/Missing.cc"]}
        (repo.root / ".night-run/scope.json").write_text(json.dumps(scope))
        manifest = fom.build(repo.root, "HEAD", fom.load_write_scope(repo.root, ".night-run/scope.json"))
        self.assertTrue(repo.entry(manifest, "native/host/A.cc")["in_write_scope"])
        self.assertFalse(repo.entry(manifest, "native/host/B.cc")["in_write_scope"])
        self.assertEqual(manifest["summary"]["include_in_write_scope"], 1)

    def test_cli_writes_once_and_reports_counts(self):
        repo = Repo(self)
        repo.write("native/host/A.cc")
        repo.write("src/U.cc")
        repo.commit()
        argv = ["--root", str(repo.root), "--output", ".night-run/m.json"]
        with mock.patch("sys.stdout"), mock.patch("sys.stderr"):
            self.assertEqual(fom.main(argv), 0)
            first = (repo.root / ".night-run/m.json").read_bytes()
            self.assertEqual(fom.main(argv), 2)
        self.assertEqual((repo.root / ".night-run/m.json").read_bytes(), first)
        counts = json.loads(first)["summary"]["counts"]
        self.assertEqual(counts, {"include": 1, "exclude": 0, "review": 1})


class RiskTags(unittest.TestCase):
    def test_risk_tags_are_recorded(self):
        repo = Repo(self)
        repo.write("native/host/R.cc", b'#define X(a) #a\n#ifdef _WIN32\n#endif\n#include "../host/app/main.cc"\n'
                                       b'using std::string;\nusing namespace std;\nauto s = R"x(raw)x";\n'
                                       b"#define Y \\\n 1\n")
        repo.write("native/host/Plain.cc", b"using namespace std;\nint x;\n")
        repo.commit()
        manifest = repo.manifest()
        self.assertEqual(repo.entry(manifest, "native/host/R.cc")["risks"],
                         ["conditional", "define", "includes-source-file", "line-splice",
                          "platform-conditional", "raw-string", "using-declaration"])
        self.assertEqual(repo.entry(manifest, "native/host/Plain.cc")["risks"], [])


if __name__ == "__main__":
    unittest.main()
