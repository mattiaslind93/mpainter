"""Fixture tests for scripts/mp.py. Every test uses its own temporary git repository."""

import importlib.util
import io
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest import mock

HERE = Path(__file__).resolve().parent
SPEC = importlib.util.spec_from_file_location("mp", HERE.parent / "mp.py")
mp = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(mp)

GIT_ENV = {"GIT_CONFIG_GLOBAL": os.devnull, "GIT_CONFIG_NOSYSTEM": "1", "GIT_AUTHOR_NAME": "f",
           "GIT_AUTHOR_EMAIL": "f@invalid", "GIT_COMMITTER_NAME": "f", "GIT_COMMITTER_EMAIL": "f@invalid"}
PYPROJECT = '[project]\nname = "mpainter"\nversion = "0.9.2"\nrequires-python = ">=3.11"\n'
CODER = "# Coder\n\n## Never\n\n- Push to `main`.\n- Publish anything to Gumroad.\n\n## Lessons\n\n- x\n"


class Repo:
    def __init__(self, test, files=None):
        tmp = tempfile.TemporaryDirectory()
        test.addCleanup(tmp.cleanup)
        self.root = Path(tmp.name).resolve() / "repo"
        self.root.mkdir()
        self.git("init", "-q", "-b", "main")
        self.write("pyproject.toml", PYPROJECT)
        self.write("scripts/verify/blender_surface.py", (HERE.parent / "verify/blender_surface.py").read_text())
        self.write("scripts/verify/bts_variables.py", (HERE.parent / "verify/bts_variables.py").read_text())
        self.write("scripts/verify/blender-ratchets.json",
                   '{"files_including_blender_outside_boundary": 99, "changed_blender_files": 99}\n')
        for rel, content in (files or {}).items():
            self.write(rel, content)
        self.git("add", "-A")
        self.git("-c", "commit.gpgsign=false", "commit", "-q", "-m", "fixture")

    def git(self, *args):
        return subprocess.run(["git", "-C", str(self.root), *args], capture_output=True, check=True,
                              env=dict(os.environ, **GIT_ENV), timeout=60).stdout

    def write(self, rel, content):
        path = self.root / rel
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(content, encoding="utf-8")

    def mp(self, *argv):
        out = io.StringIO()
        with mock.patch("sys.stderr", io.StringIO()) as err:
            code = mp.main(list(argv), root=self.root, out=out)
        return code, out.getvalue(), err.getvalue()


def statuses(repo):
    return {name: (status, details) for name, status, _, details in mp.run_checks(repo.root)}


class Version(unittest.TestCase):
    def test_reads_pyproject(self):
        repo = Repo(self)
        self.assertEqual(repo.mp("version")[:2], (0, "0.9.2\n"))

    def test_bump_writes_pyproject_and_changelog(self):
        repo = Repo(self, {"CHANGELOG.md": "# Changelog\n\n## 0.9.2\n\n- old\n"})
        code, out, _ = repo.mp("version", "bump", "0.9.3")
        self.assertEqual(code, 0, out)
        self.assertIn('version = "0.9.3"', (repo.root / "pyproject.toml").read_text())
        self.assertEqual((repo.root / "CHANGELOG.md").read_text(), "# Changelog\n\n## 0.9.3\n\n## 0.9.2\n\n- old\n")

    def test_bump_creates_changelog(self):
        repo = Repo(self)
        self.assertEqual(repo.mp("version", "bump", "1.0.0")[0], 0)
        self.assertEqual((repo.root / "CHANGELOG.md").read_text(), "# Changelog\n\n## 1.0.0\n\n")

    def test_bump_refuses_bad_or_older_versions_and_changes_nothing(self):
        repo = Repo(self, {"CHANGELOG.md": "# Changelog\n"})
        before = (repo.root / "pyproject.toml").read_bytes(), (repo.root / "CHANGELOG.md").read_bytes()
        for value in ("0.9.2", "0.9.1", "1.0", "v1.0.0", "1.0.0-rc1", ""):
            with self.subTest(value):
                self.assertEqual(repo.mp("version", "bump", value)[0], 2)
        self.assertEqual(((repo.root / "pyproject.toml").read_bytes(), (repo.root / "CHANGELOG.md").read_bytes()),
                         before)

    def test_bump_refuses_duplicate_heading(self):
        repo = Repo(self, {"CHANGELOG.md": "# Changelog\n\n## 0.9.3\n"})
        self.assertEqual(repo.mp("version", "bump", "0.9.3")[0], 2)
        self.assertNotIn("0.9.3", (repo.root / "pyproject.toml").read_text())


class Checks(unittest.TestCase):
    def test_clean_tree_passes(self):
        repo = Repo(self, {"native/host/A.cc": "#include <QUuid>\nQUuid id = QUuid();\n",
                           "native/host/CMakeLists.txt": "add_executable(x A.cc)\n"})
        result = statuses(repo)
        for name in ("version", "quuid-reset", "shared-ptr-unique", "cmake-backslash", "test-includes",
                     "blender-boundary"):
            self.assertEqual(result[name][0], "PASS", (name, result[name]))

    def test_each_gate_fails_on_a_seeded_violation(self):
        repo = Repo(self, {
            "native/host/Version.cc": 'const char *v = "0.9.2";\n',
            "packaging/Info.plist": "<key>CFBundleVersion</key><string>0.9.0-alpha</string>\n",
            "scripts/package.py": "name = 'mPainter-0.9.0-alpha-x64'\n",
            "native/host/Ids.cc": "QUuid id = {};\nvoid f(){ layer.uuid = {}; }\n",
            "native/host/Ptr.cc": "bool only = ptr.unique();\n",
            "native/host/CMakeLists.txt": 'set(X "C:\\\\Qt\\\\6.8")\n',
            "native/host/Panel.cc": '#include "../tests/Fixture.hh"\n',
            "native/host/Blend.cc": '#include "BKE_context.hh"\n',
        })
        result = statuses(repo)
        self.assertEqual(result["version"][0], "FAIL")
        self.assertTrue(any(h.startswith("native/host/Version.cc:1") for h in result["version"][1]))
        self.assertTrue(any("Info.plist" in h and "0.9.0-alpha" in h for h in result["version"][1]))
        self.assertTrue(any("scripts/package.py" in h for h in result["version"][1]))
        self.assertEqual(result["quuid-reset"], ("FAIL", ["native/host/Ids.cc:1", "native/host/Ids.cc:2"]))
        self.assertEqual(result["shared-ptr-unique"], ("FAIL", ["native/host/Ptr.cc:1"]))
        self.assertEqual(result["cmake-backslash"][0], "FAIL")
        self.assertEqual(result["test-includes"], ("FAIL", ["native/host/Panel.cc:1"]))
        self.assertEqual(result["blender-boundary"], ("WARN", ["native/host/Blend.cc"]))

    def test_version_in_tool_tests_is_allowed(self):
        repo = Repo(self, {"scripts/tests/test_x.py": "PYPROJECT = 'version = \"0.9.2\"'\n",
                           "scripts/verify/tests/test_y.py": "name = 'mPainter-0.9.0-alpha-x64'\n"})
        self.assertEqual(statuses(repo)["version"], ("PASS", []))

    def test_version_in_docs_and_other_versions_are_allowed(self):
        repo = Repo(self, {"docs/history/REPORT.md": "Shipped 0.9.2\n", "REPORT-x.md": "0.9.2\n",
                           "native/host/Qt.cc": 'const char *qt = "6.8.3"; const char *b = "10.9.21";\n'})
        self.assertEqual(statuses(repo)["version"][0], "PASS")

    def test_blender_includes_inside_the_boundary_pass(self):
        repo = Repo(self, {"native/host/blender/Bridge.cc": '#include "BKE_context.hh"\n'})
        self.assertEqual(statuses(repo)["blender-boundary"][0], "PASS")

    def test_blender_surface_doc_gate(self):
        repo = Repo(self, {"native/host/A.cc": '#include "BKE_context.hh"\n', "docs/x.md": ""})
        self.assertEqual(statuses(repo)["blender-surface"][0], "UNAVAILABLE")
        surface = mp.blender_surface(repo.root)
        repo.write("docs/blender-surface.md", surface.render(*surface.classify(repo.root)))
        self.assertEqual(statuses(repo)["blender-surface"][0], "PASS")
        repo.write("native/host/B.cc", '#include "WM_api.hh"\n')
        repo.git("add", "-A")
        self.assertEqual(statuses(repo)["blender-surface"][0], "FAIL")

    def test_docs_writes_both_pages_and_the_gate_tracks_them(self):
        repo = Repo(self, {
            "native/host/A.cc": 'auto a = std::getenv("BTS_ALPHA_DEMO"); bool b = qEnvironmentVariableIsSet("BTS_BETA");\n',
            "native/harness/D.cc": 'auto q = qgetenv("BTS_ALPHA_DEMO"); auto s = "BTS_NOT_READ";\n'})
        self.assertEqual(statuses(repo)["bts-variables"][0], "UNAVAILABLE")
        self.assertEqual(repo.mp("docs", "--check")[0], 1)
        code, out, _ = repo.mp("docs")
        self.assertEqual(code, 0, out)
        self.assertIn("wrote docs/architecture/bts-variables.md", out)
        page = (repo.root / "docs/architecture/bts-variables.md").read_text()
        self.assertIn("| `BTS_ALPHA_DEMO` | native/harness, native/host | `D.cc`, `A.cc` |", page)
        self.assertIn("`BTS_BETA`", page)
        self.assertNotIn("BTS_NOT_READ", page)
        self.assertEqual(repo.mp("docs", "--check")[:2], (0, "generated docs are current\n"))
        repo.git("add", "-A")
        self.assertEqual(statuses(repo)["bts-variables"][0], "PASS")
        repo.write("native/host/B.cc", 'auto c = getenv("BTS_GAMMA");\n')
        repo.git("add", "-A")
        self.assertEqual(statuses(repo)["bts-variables"][0], "FAIL")

    PATCH = ("diff --git a/source/a.cc b/source/a.cc\n--- a/source/a.cc\n+++ b/source/a.cc\n@@ -1 +1 @@\n-x\n+y\n"
             "diff --git a/source/new.hh b/source/new.hh\nnew file mode 100644\n--- /dev/null\n+++ b/source/new.hh\n")

    def test_blender_ratchets(self):
        repo = Repo(self, {"third_party/blender/patches/0001-x.patch": self.PATCH,
                           "third_party/blender/patches/0002-y.patch": self.PATCH.replace("a.cc", "b.cc"),
                           "native/host/A.cc": '#include "BKE_main.hh"\n',
                           "native/host/blender/B.cc": '#include "BKE_main.hh"\n'})
        (repo.root / "scripts/verify/blender-ratchets.json").unlink()
        repo.git("add", "-A")
        result = statuses(repo)
        self.assertEqual(result["blender-patch-count"][0], "UNAVAILABLE")
        self.assertEqual(result["blender-boundary"][0], "UNAVAILABLE")
        self.assertEqual(mp.blender_patch_files(repo.root, mp.git_files(repo.root)),
                         (["source/a.cc", "source/b.cc"], ["source/new.hh"]))
        repo.write("scripts/verify/blender-ratchets.json",
                   '{"files_including_blender_outside_boundary": 1, "changed_blender_files": 2}\n')
        repo.git("add", "-A")
        result = statuses(repo)
        self.assertEqual(result["blender-patch-count"][0], "PASS")
        self.assertEqual(result["blender-boundary"], ("WARN", ["native/host/A.cc"]))
        repo.write("native/host/C.cc", '#include "WM_api.hh"\n')
        repo.write("third_party/blender/patches/0003-z.patch", self.PATCH.replace("a.cc", "c.cc"))
        repo.git("add", "-A")
        result = statuses(repo)
        self.assertEqual(result["blender-boundary"][0], "FAIL")
        self.assertEqual(result["blender-patch-count"][0], "FAIL")

    def test_blender_gates_are_unavailable_without_the_generator(self):
        repo = Repo(self)
        (repo.root / "scripts/verify/blender_surface.py").unlink()
        result = statuses(repo)
        self.assertEqual(result["blender-boundary"][0], "UNAVAILABLE")
        self.assertEqual(result["blender-surface"][0], "UNAVAILABLE")

    def test_untracked_and_symlinked_files_are_not_checked(self):
        repo = Repo(self, {"native/host/A.cc": "int a;\n"})
        repo.write("native/host/Untracked.cc", "QUuid id = {};\n")
        outside = repo.root.parent / "outside.cc"
        outside.write_text("QUuid id = {};\n")
        os.symlink(outside, repo.root / "native/host/Link.cc")
        repo.git("add", "native/host/Link.cc")
        self.assertEqual(statuses(repo)["quuid-reset"], ("PASS", []))

    def test_agent_docs_gate_uses_the_checker_and_is_unavailable_without_it(self):
        repo = Repo(self)
        self.assertEqual(statuses(repo)["agent-docs"][0], "UNAVAILABLE")
        checker = (HERE.parent / "verify/check_agent_docs.py").read_text()
        repo.write("scripts/verify/check_agent_docs.py", checker)
        repo.git("add", "-A")
        self.assertEqual(statuses(repo)["agent-docs"][0], "FAIL")  # no AGENTS.md yet
        repo.write("AGENTS.md", "Version in `pyproject.toml`.\n")
        repo.write("CLAUDE.md", "@AGENTS.md\n")
        repo.git("add", "-A")
        self.assertEqual(statuses(repo)["agent-docs"][0], "PASS")

    def test_unpushed_without_upstream_is_unavailable_not_pass(self):
        self.assertEqual(statuses(Repo(self))["unpushed"][0], "UNAVAILABLE")

    def test_cli_exit_codes_and_not_yet_list(self):
        repo = Repo(self, {"native/host/Ptr.cc": "bool only = p.unique();\n"})
        code, out, _ = repo.mp("check")
        self.assertEqual(code, 1)
        self.assertIn("FAIL        shared-ptr-unique", out)
        self.assertIn("NOT YET     clang-format", out)
        self.assertEqual(repo.mp("check", "--only", "version")[0], 0)
        self.assertEqual(repo.mp("check", "--only", "nonsense")[0], 2)

    def test_check_never_writes(self):
        repo = Repo(self, {"native/host/A.cc": "int a;\n"})
        before = {p: p.read_bytes() for p in repo.root.rglob("*") if p.is_file() and ".git" not in p.parts}
        repo.mp("check")
        after = {p: p.read_bytes() for p in repo.root.rglob("*") if p.is_file() and ".git" not in p.parts}
        self.assertEqual(before, after)


class Move(unittest.TestCase):
    CMAKE = ("add_executable(bts-host ${BTS_HOST_SOURCE_DIR}/Brush.cc ${BTS_HOST_SOURCE_DIR}/BrushJitter.cc\n"
             "  ${BTS_HOST_SOURCE_DIR}/Other.cc)\n")

    def repo(self):
        return Repo(self, {"native/host/CMakeLists.txt": self.CMAKE, "native/host/Brush.cc": "x\n",
                           "native/host/Brush.hh": "x\n", "native/host/BrushJitter.cc": "y\n",
                           "native/host/Other.cc": "z\n",
                           "scripts/host/verify-brush.py": "a = 'native/host/Brush.cc'; b = 'native/host/Brush.ccx'\n",
                           "native/tests/brush_test.cc": '#include "../host/Brush.hh"\n#include "../host/Other.hh"\n'})

    def move(self, repo, *names, refs=False, allow=True):
        listing = repo.root.parent / "list.txt"
        listing.write_text("".join(f"native/host/{n}\n" for n in names))
        return repo.mp("move", "native/host/painting", "--list", str(listing), *(["--refs"] if refs else []),
                       *(["--allow-referenced"] if allow else []))

    def test_referenced_files_are_refused_by_default(self):
        repo = self.repo()
        repo.write("scripts/host/verify-stem.py", "src = ['native/host/' + s + '.cc' for s in ['BrushJitter']]\n")
        repo.git("add", "-A")
        code, _, err = self.move(repo, "BrushJitter.cc", allow=False)
        self.assertEqual(code, 2)
        self.assertIn("scripts/host/verify-stem.py:1: BrushJitter.cc", err)
        code, _, err = self.move(repo, "Brush.cc", allow=False)
        self.assertEqual(code, 2)
        self.assertIn("native/host/Brush.cc", (repo.root / "scripts/host/verify-brush.py").read_text())
        self.assertTrue((repo.root / "native/host/Brush.cc").is_file())
        self.assertEqual(self.move(repo, "Other.cc", allow=False)[0], 0)

    def test_bare_includes_in_tests_do_not_block_but_relative_ones_do(self):
        repo = self.repo()
        repo.write("native/tests/jitter_test.cc", '#include "BrushJitter.cc"\n')
        repo.git("add", "-A")
        self.assertEqual(self.move(repo, "BrushJitter.cc", allow=False)[0], 0)
        code, _, err = self.move(repo, "Brush.hh", allow=False)
        self.assertEqual(code, 2)
        self.assertIn("native/tests/brush_test.cc:1: Brush.hh", err)

    def test_move_rewrites_cmake_exactly(self):
        repo = self.repo()
        code, out, err = self.move(repo, "Brush.cc", "Brush.hh")
        self.assertEqual(code, 0, err)
        self.assertIn("2 files", out)
        cmake = (repo.root / "native/host/CMakeLists.txt").read_text()
        self.assertIn("${BTS_HOST_SOURCE_DIR}/painting/Brush.cc ${BTS_HOST_SOURCE_DIR}/BrushJitter.cc", cmake)
        self.assertTrue((repo.root / "native/host/painting/Brush.cc").is_file())
        self.assertFalse((repo.root / "native/host/Brush.cc").exists())
        self.assertIn("native/host/Brush.cc", (repo.root / "scripts/host/verify-brush.py").read_text())
        status = repo.git("status", "--porcelain").decode()
        self.assertIn("R  native/host/Brush.cc -> native/host/painting/Brush.cc", status)

    def test_refs_rewrite_scripts_and_test_includes(self):
        repo = self.repo()
        self.assertEqual(self.move(repo, "Brush.cc", "Brush.hh", refs=True)[0], 0)
        self.assertEqual((repo.root / "scripts/host/verify-brush.py").read_text(),
                         "a = 'native/host/painting/Brush.cc'; b = 'native/host/Brush.ccx'\n")
        self.assertEqual((repo.root / "native/tests/brush_test.cc").read_text(),
                         '#include "../host/painting/Brush.hh"\n#include "../host/Other.hh"\n')

    def test_move_refuses_bad_input_and_changes_nothing(self):
        repo = self.repo()
        before = (repo.root / "native/host/CMakeLists.txt").read_text()
        for names in (("Missing.cc",), ("Brush.cc", "Brush.cc")):
            with self.subTest(names):
                self.assertEqual(self.move(repo, *names)[0], 2)
        listing = repo.root.parent / "list.txt"
        listing.write_text("native/host/Brush.cc\n")
        for dest in ("native/other", "native/host/../x", "native/host/Painting"):
            self.assertEqual(repo.mp("move", dest, "--list", str(listing))[0], 2)
        self.assertEqual((repo.root / "native/host/CMakeLists.txt").read_text(), before)
        self.assertTrue((repo.root / "native/host/Brush.cc").is_file())


class Doctor(unittest.TestCase):
    def fake(self, outputs):
        def run(argv, **kwargs):
            key = argv[0] if argv[0] != "distrobox" or "enter" not in argv else "bts-build"
            code, stdout = outputs[key]
            return SimpleNamespace(returncode=code, stdout=stdout, stderr="")
        return run

    def test_reports_present_missing_and_wrong_tools(self):
        repo = Repo(self)
        tools = {"git": (0, "git version 2.50\n"), "distrobox": (0, "distrobox: 1.8\n"),
                 "bts-build": (0, "clang-format version 20.1.0\n")}
        rows = mp.doctor(repo.root, which=lambda name: name in ("git", "distrobox"), run=self.fake(tools))
        result = {name: status for name, status, _ in rows}
        self.assertEqual(result["python"], "OK")
        self.assertEqual(result["git"], "OK")
        self.assertEqual(result["cmake"], "UNAVAILABLE")
        self.assertEqual(result["clang-format in bts-build"], "WRONG")

    def test_missing_git_is_a_failure(self):
        repo = Repo(self)
        rows = mp.doctor(repo.root, which=lambda name: False, run=self.fake({}))
        self.assertIn(("git", "MISSING", "git not on PATH"), rows)


class Brief(unittest.TestCase):
    ARGS = ["--goal", "Format native/host", "--platform", "Linux, Bazzite", "--base", "a" * 40,
            "--worktree", "~/w", "--paths", "native/host/**", "--proof", "python3 x.py exits 0",
            "--report", "~/runs/x/report.md", "--forbid", "Touch scripts/platforms/macos/."]

    def test_brief_has_every_field_and_the_never_list(self):
        repo = Repo(self, {"docs/agents/coder.md": CODER})
        output = repo.root / "brief.md"
        code, _, err = repo.mp("brief", "--output", str(output), *self.ARGS)
        self.assertEqual(code, 0, err)
        brief = output.read_text()
        for needle in ("Goal: Format native/host", "Platform and machine: Linux, Bazzite", "a" * 40,
                       "- `native/host/**`", "- Push to `main`.", "- Publish anything to Gumroad.",
                       "- Touch scripts/platforms/macos/.", "Proof: python3 x.py exits 0", "Report: ~/runs/x"):
            self.assertIn(needle, brief)
        self.assertNotIn("Lessons", brief)

    def test_brief_refuses_missing_fields_short_sha_existing_output_or_no_guide(self):
        repo = Repo(self, {"docs/agents/coder.md": CODER})
        output = repo.root / "brief.md"
        self.assertEqual(repo.mp("brief", "--output", str(output), "--goal", "x")[0], 2)
        bad_sha = [a if a != "a" * 40 else "abc123" for a in self.ARGS]
        self.assertEqual(repo.mp("brief", "--output", str(output), *bad_sha)[0], 2)
        self.assertFalse(output.exists())
        output.write_text("keep")
        self.assertEqual(repo.mp("brief", "--output", str(output), *self.ARGS)[0], 2)
        self.assertEqual(output.read_text(), "keep")
        (repo.root / "docs/agents/coder.md").unlink()
        self.assertEqual(repo.mp("brief", "--output", str(repo.root / "b2.md"), *self.ARGS)[0], 2)


if __name__ == "__main__":
    unittest.main()
