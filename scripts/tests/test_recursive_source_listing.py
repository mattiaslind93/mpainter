"""Scripts must list owned source folders recursively.

The restructure plan moves native/host into domain subfolders. A script that
lists native/host with glob('*'), iterdir() or os.listdir() would silently stop
seeing the moved files and pass on nothing. This test reads every tracked
Python file as data (ast) and fails on such a listing.
"""

import ast
import os
import subprocess
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
OWNED = ("native/host", "native/harness", "native/tests")
FLAT = {"iterdir", "listdir", "scandir"}


def mentions_owned(node):
    return any(isinstance(n, ast.Constant) and isinstance(n.value, str)
               and any(folder in n.value or n.value.strip("/") == folder.split("/")[-1] and n.value != ""
                       for folder in OWNED)
               for n in ast.walk(node))


def owned_names(tree):
    """Names assigned an expression that spells an owned folder, such as host = root/'native/host'."""
    names = set()
    for node in ast.walk(tree):
        if isinstance(node, ast.Assign) and mentions_owned(node.value):
            names.update(t.id for t in node.targets if isinstance(t, ast.Name))
    return names


def flat_listings(source):
    tree = ast.parse(source)
    names = owned_names(tree)
    found = []
    for node in ast.walk(tree):
        if not isinstance(node, ast.Call):
            continue
        func = node.func
        if isinstance(func, ast.Attribute) and func.attr in FLAT | {"glob"}:
            receiver = func.value
            owned = mentions_owned(receiver) or (isinstance(receiver, ast.Name) and receiver.id in names)
            if func.attr == "glob":
                pattern = node.args[0].value if node.args and isinstance(node.args[0], ast.Constant) else None
                flat = isinstance(pattern, str) and "**" not in pattern
            elif func.attr in ("listdir", "scandir"):  # os.listdir(path)
                owned = any(mentions_owned(a) or (isinstance(a, ast.Name) and a.id in names) for a in node.args)
                flat = True
            else:
                flat = True
            if owned and flat:
                found.append(node.lineno)
    return sorted(found)


def tracked_python():
    out = subprocess.run(["git", "-C", str(ROOT), "ls-files", "-z", "--", "*.py"], capture_output=True,
                         check=True, timeout=60).stdout
    return [os.fsdecode(p) for p in out.split(b"\0") if p]


class RecursiveSourceListing(unittest.TestCase):
    def test_detector_catches_flat_listings(self):
        flat = ("from pathlib import Path\nimport os\nroot = Path('.')\nhost = root / 'native/host'\n"
                "a = list(host.glob('*'))\nb = list((root / 'native/host').iterdir())\n"
                "c = os.listdir('native/tests')\nd = list((root/'native/host').glob('*.cc'))\n")
        self.assertEqual(flat_listings(flat), [5, 6, 7, 8])

    def test_detector_accepts_recursive_and_unrelated_listings(self):
        fine = ("from pathlib import Path\nroot = Path('.')\nhost = root / 'native/host'\n"
                "a = list(host.rglob('*'))\nb = list((root/'native/host').glob('**/*.cc'))\n"
                "c = list((root/'assets/grunges').glob('*'))\nd = list(Path('out').iterdir())\n")
        self.assertEqual(flat_listings(fine), [])

    def test_every_tracked_python_file_parses(self):
        # Scripted path rewrites must not leave a verifier that cannot even be read.
        broken = []
        for rel in tracked_python():
            path = ROOT / rel
            if path.is_file() and not path.is_symlink():
                try:
                    ast.parse(path.read_text(encoding="utf-8"), filename=rel)
                except SyntaxError as error:
                    broken.append(f"{rel}:{error.lineno}: {error.msg}")
        self.assertEqual(broken, [])

    def test_no_tracked_script_lists_owned_sources_flat(self):
        problems = []
        for rel in tracked_python():
            path = ROOT / rel
            if path.is_symlink() or not path.is_file():
                continue
            try:
                lines = flat_listings(path.read_text(encoding="utf-8"))
            except SyntaxError:
                continue
            problems.extend(f"{rel}:{line}" for line in lines)
        self.assertEqual(problems, [], "list native/host and native/tests with rglob or glob('**/...')")


if __name__ == "__main__":
    unittest.main()
