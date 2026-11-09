"""Fixture tests for scripts/verify/blender_surface.py."""

import contextlib
import importlib.util
import io
import os
import subprocess
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
SPEC = importlib.util.spec_from_file_location("blender_surface", HERE.parent / "blender_surface.py")
bs = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(bs)
GIT_ENV = {"GIT_CONFIG_GLOBAL": os.devnull, "GIT_CONFIG_NOSYSTEM": "1"}


class Fixture:
    def __init__(self, test, files):
        tmp = tempfile.TemporaryDirectory()
        test.addCleanup(tmp.cleanup)
        self.root = Path(tmp.name).resolve() / "repo"
        self.root.mkdir()
        subprocess.run(["git", "-C", str(self.root), "init", "-q"], check=True, env=dict(os.environ, **GIT_ENV))
        for rel, text in files.items():
            path = self.root / rel
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(text)
        subprocess.run(["git", "-C", str(self.root), "add", "-A"], check=True, env=dict(os.environ, **GIT_ENV))

    def run(self, *argv):
        out = io.StringIO()
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(io.StringIO()):
            code = bs.main(["--root", str(self.root), *argv])
        return code, out.getvalue()


FILES = {
    "native/host/A.cc": '#include "A.hh"\n#include "BKE_context.hh"\n#include "GPU_shader.hh"\n'
                        "#include <QWidget>\n#include <vector>\n#include <pxr/usd/usd/stage.h>\n",
    "native/host/A.hh": '#include "GPU_shader.hh"\n// #include "BLI_commented.hh" is not at line start\n',
    "native/host/blender/Bridge.cc": '#include "DNA_mesh_types.h"\n',
    "native/tests/a_test.cc": '#include "../host/A.hh"\n#include "Fixture.hh"\n#include "BKE_context.hh"\n',
    "native/tests/Fixture.hh": "int x;\n",
    "native/host/OwnedLooking_X.hh": "int owned;\n",
    "native/host/B.cc": '#include "OwnedLooking_X.hh"\n',
}


class BlenderSurface(unittest.TestCase):
    def test_classification(self):
        fixture = Fixture(self, FILES)
        surface, others, includers = bs.classify(fixture.root)
        self.assertEqual(sorted(surface), ["BKE", "DNA", "GPU"])
        self.assertEqual(surface["BKE"]["BKE_context.hh"], {"native/host/A.cc", "native/tests/a_test.cc"})
        self.assertEqual(surface["GPU"]["GPU_shader.hh"], {"native/host/A.cc", "native/host/A.hh"})
        self.assertEqual(sorted(others), ["pxr/usd/usd/stage.h"])
        self.assertNotIn("native/host/B.cc", includers)  # resolves to a tracked owned file
        self.assertEqual(sorted(includers), ["native/host/A.cc", "native/host/A.hh",
                                             "native/host/blender/Bridge.cc", "native/tests/a_test.cc"])

    def test_render_counts_files_outside_the_boundary(self):
        fixture = Fixture(self, FILES)
        text = bs.render(*bs.classify(fixture.root))
        self.assertIn("- Files that include them: 4, of which 3 are outside `native/host/blender/`", text)
        self.assertIn("| BKE | `BKE_context.hh` | 2 |", text)
        self.assertNotIn("BLI_commented", text)

    def test_write_then_check_then_stale(self):
        fixture = Fixture(self, FILES)
        self.assertEqual(fixture.run("--check", "docs/blender-surface.md")[0], 1)
        (fixture.root / "docs").mkdir()
        self.assertEqual(fixture.run("--write", "docs/blender-surface.md")[0], 0)
        self.assertEqual(fixture.run("--check", "docs/blender-surface.md"), (0, "docs/blender-surface.md is current\n"))
        (fixture.root / "native/host/C.cc").write_text('#include "WM_api.hh"\n')
        subprocess.run(["git", "-C", str(fixture.root), "add", "-A"], check=True, env=dict(os.environ, **GIT_ENV))
        self.assertEqual(fixture.run("--check", "docs/blender-surface.md")[0], 1)

    def test_untracked_files_and_outside_output_are_ignored_or_refused(self):
        fixture = Fixture(self, FILES)
        (fixture.root / "native/host/Untracked.cc").write_text('#include "BKE_untracked.hh"\n')
        self.assertNotIn("BKE_untracked.hh", bs.render(*bs.classify(fixture.root)))
        self.assertEqual(fixture.run("--write", "../outside.md")[0], 2)
        self.assertFalse((fixture.root.parent / "outside.md").exists())

    def test_harness_headers_resolve_like_the_build(self):
        fixture = Fixture(self, {"native/host/A.cc": '#include "XDemo.hh"\n#include "BKE_main.hh"\n',
                                 "native/harness/XDemo.hh": '#include "A.hh"\n', "native/host/A.hh": ""})
        _, others, includers = bs.classify(fixture.root)
        self.assertEqual(others, {})
        self.assertEqual(sorted(includers), ["native/host/A.cc"])

    def test_deterministic(self):
        fixture = Fixture(self, FILES)
        self.assertEqual(bs.render(*bs.classify(fixture.root)), bs.render(*bs.classify(fixture.root)))


if __name__ == "__main__":
    unittest.main()
