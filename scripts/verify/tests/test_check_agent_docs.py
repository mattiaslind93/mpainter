# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for scripts/verify/check_agent_docs.py, run against throwaway repository trees."""
import ast
import contextlib
import io
import os
import pathlib
import sys
import tempfile
import unittest
from unittest import mock

VERIFY = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(VERIFY))

import check_agent_docs  # noqa: E402

CLAUDE = "@AGENTS.md\n"
CMAKE = "add_custom_target(bts-cpu-check)\nadd_executable(bts-host main.cc)\n"


class Fixture:
    """A repository at <tmp>/repo. <tmp>/outside is a sibling the checker must never treat as part of it."""

    def __init__(self, files):
        self._tmp = tempfile.TemporaryDirectory()
        self.top = pathlib.Path(self._tmp.name).resolve()
        self.root = self.top / "repo"
        self.outside = self.top / "outside"
        self.root.mkdir()
        self.outside.mkdir()
        for rel, text in files.items():
            self.write(rel, text)

    def write(self, rel, text):
        path = self.root / rel
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8")

    def write_outside(self, rel, text):
        path = self.outside / rel
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8")
        return path

    def link(self, rel, target):
        try:
            os.symlink(target, self.root / rel)
        except (OSError, NotImplementedError) as error:
            raise unittest.SkipTest(f"symlinks unavailable: {error}")

    def close(self):
        self._tmp.cleanup()

    def check(self, *extra):
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            code = check_agent_docs.main(["--root", str(self.root), *extra])
        return code, out.getvalue()

    def snapshot(self):
        """Every entry under the temp folder, without following symlinks."""
        state = {}
        for folder, dirs, names in os.walk(self.top):
            for name in dirs + names:
                path = pathlib.Path(folder) / name
                stat = path.lstat()
                content = None
                if path.is_symlink():
                    content = os.readlink(path)
                elif path.is_file():
                    content = path.read_bytes()
                state[path.relative_to(self.top).as_posix()] = (stat.st_mode, stat.st_mtime_ns, content)
        return state


@contextlib.contextmanager
def watch_io():
    """Record every file the checker opens, and fail on any attempt to run a program."""
    opened = []
    real_open = io.open

    def recording_open(file, mode="r", *args, **kwargs):
        opened.append((pathlib.Path(os.fspath(file)).resolve(), mode))
        return real_open(file, mode, *args, **kwargs)

    def refuse(*args, **kwargs):
        raise AssertionError(f"checker tried to run something: {args!r}")

    runners = ["subprocess.Popen", "os.system", "os.popen", "os.fork", "os.forkpty"]
    runners += [f"os.{name}" for name in dir(os) if name.startswith(("exec", "spawn", "posix_spawn"))]
    with contextlib.ExitStack() as stack:
        stack.enter_context(mock.patch("io.open", recording_open))
        stack.enter_context(mock.patch("builtins.open", recording_open))
        for target in runners:
            stack.enter_context(mock.patch(target, side_effect=refuse))
        yield opened


def repo(agents, **extra):
    files = {
        "AGENTS.md": agents,
        "CLAUDE.md": CLAUDE,
        "pyproject.toml": "",
        "scripts/build.py": "",
        "scripts/cross-windows/env.sh": "",
        "native/host/CMakeLists.txt": CMAKE,
        "native/host/app/main.cc": "",
        "third_party/blender/patches/0001-native-host.patch": "",
    }
    files.update(extra)
    return Fixture(files)


class CheckAgentDocs(unittest.TestCase):
    def run_repo(self, agents, *flags, **extra):
        fixture = repo(agents, **extra)
        self.addCleanup(fixture.close)
        return fixture.check(*flags)

    def test_existing_paths_and_commands_pass(self):
        code, out = self.run_repo(
            "# Repo\n"
            "Version in `pyproject.toml`, host in `native/host/`, file `native/host/app/main.cc`.\n"
            "Patch `0001-native-host.patch`.\n"
            "| Build | `python3 scripts/build.py --blender ../build/blender --qt <qt-root>` |\n"
            "| Test | `cmake --build <build> --target bts-cpu-check` |\n"
            "```sh\n$ python3 scripts/build.py --help\n```\n"
        )
        self.assertEqual(code, 0, out)
        self.assertIn("0 problem(s)", out)
        self.assertIn("7 reference(s) found", out)

    def test_broken_path_fails_with_line(self):
        code, out = self.run_repo("ok\nSee `scripts/missing.py` and `docs/gone/`.\n")
        self.assertEqual(code, 1)
        self.assertIn("AGENTS.md:2: `scripts/missing.py` does not exist", out)
        self.assertIn("AGENTS.md:2: `docs/gone/` does not exist", out)

    def test_broken_command_script_fails(self):
        code, out = self.run_repo("Run `python3 scripts/renamed.py --flag`.\n")
        self.assertEqual(code, 1)
        self.assertIn("`scripts/renamed.py` does not exist", out)

    def test_command_script_name_is_not_found_by_basename_elsewhere(self):
        code, out = self.run_repo("Run `python3 build.py`.\n")
        self.assertEqual(code, 1)
        self.assertIn("`build.py` does not exist", out)

    def test_broken_shell_fence_command_fails(self):
        code, out = self.run_repo("```bash\nsource scripts/cross-windows/old-env.sh\n```\n")
        self.assertEqual(code, 1)
        self.assertIn("AGENTS.md:2: `scripts/cross-windows/old-env.sh` does not exist", out)

    def test_folder_relative_command_passes(self):
        fixture = repo("root\n")
        self.addCleanup(fixture.close)
        (fixture.root / "scripts/cross-windows/AGENTS.md").write_text("1. `source env.sh`.\n", encoding="utf-8")
        (fixture.root / "scripts/cross-windows/CLAUDE.md").write_text(CLAUDE, encoding="utf-8")
        code, out = fixture.check()
        self.assertEqual(code, 0, out)

    def test_undefined_cmake_target_fails(self):
        code, out = self.run_repo("`cmake --build <build> --target bts-renamed-check`\n")
        self.assertEqual(code, 1)
        self.assertIn("CMake target `bts-renamed-check` is not defined", out)

    def test_cmake_without_target_name_fails(self):
        code, out = self.run_repo("`cmake --build out --target`\n")
        self.assertEqual(code, 1)
        self.assertIn("has no target after --target", out)

    def test_placeholders_and_platform_examples_are_not_checked_not_passed(self):
        code, out = self.run_repo(
            "`native/tests/<name>_test.cc`, `*_test.cc`, `bts-cpu-tests-<config>.json`, "
            "`D:\\a\\mpainter`, `~/mpainter-work/runs/`, `/usr/lib/x.so`, `https://example.com/a.md`, "
            "`cmake --build <build> --target bts-<name>-tests`, `<number>-<platform or area>-<what>.patch`\n",
            "--verbose",
        )
        self.assertEqual(code, 0, out)
        self.assertIn("0 reference(s) found, 9 not checked", out)
        self.assertIn("not checked (placeholder or pattern): 5", out)
        self.assertIn("not checked (platform-specific (Windows) path): 1", out)
        self.assertIn("not checked (outside the repository): 2", out)
        self.assertIn("not checked (URL): 1", out)
        self.assertIn("AGENTS.md:1: `D:\\a\\mpainter`", out)

    def test_unsupported_and_compound_commands_are_reported(self):
        code, out = self.run_repo(
            "`git status --short`, `cd native && python3 x.py`, `python3 -m unittest`, `python3 -c 'print(1)'`\n"
        )
        self.assertEqual(code, 0, out)
        self.assertIn("not checked (unsupported command `git`): 1", out)
        self.assertIn("not checked (compound shell command): 1", out)
        self.assertIn("not checked (python -m or -c): 2", out)

    def test_non_shell_fences_are_reported_not_checked(self):
        code, out = self.run_repo("```cmake\nadd_executable(x missing/file.cc)\n```\n```\nmissing/tree.md\n```\n")
        self.assertEqual(code, 0, out)
        self.assertIn("not checked (fenced block that is not shell): 2", out)

    def test_claude_must_be_only_the_import_line(self):
        code, out = self.run_repo("ok\n", **{"CLAUDE.md": "@AGENTS.md\nextra\n"})
        self.assertEqual(code, 1)
        self.assertIn("CLAUDE.md: must contain only @AGENTS.md", out)

    def test_agents_needs_claude_and_claude_needs_agents(self):
        fixture = repo("ok\n", **{"docs/CLAUDE.md": CLAUDE, "native/AGENTS.md": "x\n"})
        self.addCleanup(fixture.close)
        code, out = fixture.check()
        self.assertEqual(code, 1)
        self.assertIn("docs/CLAUDE.md: no AGENTS.md beside it", out)
        self.assertIn("native/AGENTS.md: no CLAUDE.md beside it", out)

    def test_tree_without_agents_fails_instead_of_passing(self):
        fixture = Fixture({"README.md": "x\n"})
        self.addCleanup(fixture.close)
        code, out = fixture.check()
        self.assertEqual(code, 1)
        self.assertIn("no AGENTS.md found, so nothing was checked", out)
        self.assertIn("0 AGENTS.md", out)

    def test_skipped_folders_are_not_scanned(self):
        code, out = self.run_repo("ok\n", **{"build/AGENTS.md": "`missing.py`\n"})
        self.assertEqual(code, 0, out)
        self.assertIn("1 AGENTS.md", out)


class Containment(unittest.TestCase):
    def assert_read_only_inside(self, fixture, opened):
        self.assertTrue(opened, "the spy saw no reads, so it is not watching the checker")
        self.assertTrue(all(fixture.root in path.parents for path, _ in opened), opened)
        self.assertTrue(all(mode == "r" for _, mode in opened), opened)

    def test_dotdot_escape_is_not_checked_or_read(self):
        fixture = repo(
            "`../outside/secret.md` `a/../../outside/secret.md` `scripts/../../outside/secret.md` "
            "`python3 ../outside/plant.py`\n",
            **{
                "native/AGENTS.md": "`../../outside/secret.md` `../pyproject.toml` `../scripts/build.py`\n",
                "native/CLAUDE.md": CLAUDE,
            },
        )
        self.addCleanup(fixture.close)
        fixture.write_outside("secret.md", "x\n")
        fixture.write_outside("plant.py", "")
        with watch_io() as opened:
            code, out = fixture.check("--verbose")
        self.assertEqual(code, 0, out)
        self.assertIn("not checked (resolves outside the repository): 5", out)
        self.assertIn("2 reference(s) found", out)
        self.assert_read_only_inside(fixture, opened)

    def test_dotdot_that_stays_inside_and_is_missing_fails(self):
        fixture = repo("`scripts/../gone.md`\n")
        self.addCleanup(fixture.close)
        code, out = fixture.check()
        self.assertEqual(code, 1)
        self.assertIn("`scripts/../gone.md` does not exist", out)

    def test_symlink_escape_is_not_checked_or_read(self):
        fixture = repo("`link/secret.md` `secret-link.md` `alias.md` `cmake --build b --target bts-outside`\n")
        self.addCleanup(fixture.close)
        fixture.write_outside("secret.md", "x\n")
        fixture.write_outside("CMakeLists.txt", "add_custom_target(bts-outside)\n")
        fixture.write_outside("AGENTS.md", "`outside-only-missing.py`\n")
        fixture.write("docs/README.md", "")
        fixture.link("link", fixture.outside)
        fixture.link("secret-link.md", fixture.outside / "secret.md")
        fixture.link("extra.cmake", fixture.outside / "CMakeLists.txt")
        fixture.link("docs/AGENTS.md", fixture.outside / "AGENTS.md")
        fixture.link("alias.md", "pyproject.toml")
        with watch_io() as opened:
            code, out = fixture.check("--verbose")
        self.assertEqual(code, 1, out)
        self.assertIn("CMake target `bts-outside` is not defined", out)
        self.assertIn("not checked (resolves outside the repository): 2", out)
        self.assertIn("not checked (symlink that leaves the repository): 4", out)
        self.assertIn("1 AGENTS.md, 1 reference(s) found", out)
        self.assertNotIn("outside-only-missing.py", out)
        self.assert_read_only_inside(fixture, opened)


class CMakeTargets(unittest.TestCase):
    def test_commented_declarations_do_not_define_targets(self):
        cmake = (
            "# add_custom_target(bts-line-comment)\n"
            "set(X 1) # add_executable(bts-trailing-comment x.cc)\n"
            "#[[\nadd_custom_target(bts-bracket-comment)\n]]\n"
            "#[==[ add_library(bts-level-comment x.cc) ]==]\n"
            'message("# not a comment") add_custom_target(bts-after-quoted-hash)\n'
            "add_custom_target(bts-cpu-check)\n"
        )
        names = ["bts-line-comment", "bts-trailing-comment", "bts-bracket-comment", "bts-level-comment"]
        doc = "".join(f"`cmake --build b --target {n}`\n" for n in names + ["bts-after-quoted-hash", "bts-cpu-check"])
        fixture = repo(doc, **{"native/host/CMakeLists.txt": cmake})
        self.addCleanup(fixture.close)
        code, out = fixture.check()
        self.assertEqual(code, 1)
        for name in names:
            self.assertIn(f"CMake target `{name}` is not defined", out)
        self.assertIn("2 reference(s) found", out)
        self.assertIn("4 problem(s)", out)

    def test_every_target_after_the_flag_is_checked(self):
        fixture = repo(
            "`cmake --build b --target bts-cpu-check bts-missing-one`\n"
            "`cmake --build b -t bts-host --target bts-missing-two --parallel 4`\n"
            "`cmake --build b --target bts-cpu-check <more>`\n"
            "`cmake --build b --target bts-host -- bts-native-tool-arg`\n"
        )
        self.addCleanup(fixture.close)
        code, out = fixture.check("--verbose")
        self.assertEqual(code, 1)
        self.assertIn("AGENTS.md:1: CMake target `bts-missing-one` is not defined", out)
        self.assertIn("AGENTS.md:2: CMake target `bts-missing-two` is not defined", out)
        self.assertIn("AGENTS.md:3: `<more>`", out)
        self.assertNotIn("bts-native-tool-arg", out)
        self.assertIn("4 reference(s) found, 1 not checked, 2 problem(s)", out)


class BareNames(unittest.TestCase):
    def run_repo(self, agents, **extra):
        fixture = repo(agents, **extra)
        self.addCleanup(fixture.close)
        return fixture.check("--verbose")

    def test_ambiguous_bare_name_is_not_checked_not_passed(self):
        code, out = self.run_repo("`notes.md` `unique.md`\n", **{"a/notes.md": "", "b/notes.md": "", "c/unique.md": ""})
        self.assertEqual(code, 0, out)
        self.assertIn("not checked (ambiguous bare name): 1", out)
        self.assertIn("AGENTS.md:1: `notes.md`", out)
        self.assertIn("1 reference(s) found", out)

    def test_bare_name_elsewhere_than_below_the_doc_does_not_pass(self):
        code, out = self.run_repo("root\n", **{"scripts/AGENTS.md": "`main.cc`\n", "scripts/CLAUDE.md": CLAUDE})
        self.assertEqual(code, 1)
        self.assertIn("scripts/AGENTS.md:1: `main.cc` does not exist", out)

    def test_direct_match_beside_the_doc_wins_over_nested_copies(self):
        code, out = self.run_repo("`README.md`\n", **{"README.md": "", "a/README.md": "", "b/README.md": ""})
        self.assertEqual(code, 0, out)
        self.assertIn("1 reference(s) found, 0 not checked", out)


class ReadOnlyAndNonExecution(unittest.TestCase):
    TRAPS = (
        "Run `python3 scripts/plant.py`, `sh scripts/plant.sh`, `./scripts/plant.sh`, `touch SENTINEL`.\n"
        "```sh\npython3 scripts/plant.py\nbash scripts/plant.sh\n```\n"
    )

    def test_documented_commands_are_never_run_and_tree_is_unchanged(self):
        plant = "import pathlib\npathlib.Path(__file__).parent.parent.joinpath('SENTINEL').write_text('ran')\n"
        fixture = repo(
            self.TRAPS,
            **{"scripts/plant.py": plant, "scripts/plant.sh": "#!/bin/sh\ntouch \"$(dirname \"$0\")/../SENTINEL\"\n"},
        )
        self.addCleanup(fixture.close)
        os.chmod(fixture.root / "scripts/plant.sh", 0o755)
        before = fixture.snapshot()

        with watch_io() as opened:
            code, out = fixture.check("--verbose")

        self.assertEqual(code, 0, out)
        self.assertFalse((fixture.root / "SENTINEL").exists())
        self.assertEqual(fixture.snapshot(), before)
        self.assertIn((fixture.root / "AGENTS.md", "r"), opened)
        self.assertTrue(all(mode == "r" for _, mode in opened), opened)
        self.assertNotIn(fixture.root / "scripts/plant.py", [path for path, _ in opened])

    def test_module_has_no_way_to_run_code(self):
        tree = ast.parse(pathlib.Path(check_agent_docs.__file__).read_text(encoding="utf-8"))
        imported, builtins, methods = set(), set(), set()
        for node in ast.walk(tree):
            if isinstance(node, ast.Import):
                imported.update(alias.name for alias in node.names)
            elif isinstance(node, ast.ImportFrom):
                imported.add(node.module)
            elif isinstance(node, ast.Call) and isinstance(node.func, ast.Name):
                builtins.add(node.func.id)
            elif isinstance(node, ast.Call) and isinstance(node.func, ast.Attribute):
                methods.add(node.func.attr)
        self.assertFalse(imported & {"subprocess", "runpy", "importlib", "pty", "multiprocessing", "shutil"})
        self.assertFalse(builtins & {"eval", "exec", "compile", "open", "__import__"})
        self.assertFalse(
            methods
            & {"system", "popen", "Popen", "spawnv", "execv", "execvp", "posix_spawn", "write_text", "write_bytes",
               "open", "unlink", "rename", "mkdir", "rmdir", "chmod", "touch", "run_path", "symlink_to",
               "hardlink_to", "write", "symlink"}
        )

    def test_help_does_not_scan(self):
        out = io.StringIO()
        with contextlib.redirect_stdout(out), mock.patch.object(check_agent_docs, "run") as scan:
            with self.assertRaises(SystemExit) as exit:
                check_agent_docs.main(["--help"])
        self.assertEqual(exit.exception.code, 0)
        scan.assert_not_called()
        self.assertIn("--root", out.getvalue())


if __name__ == "__main__":
    unittest.main()
