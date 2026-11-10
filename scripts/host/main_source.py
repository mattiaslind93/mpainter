#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""The source of mPainter's main() as one text, for verifiers that read it.

main.cc includes the Painter session from native/host/app/ (one translation unit, see
WorkspaceSession.inc). Verifiers that check main()'s callbacks as text read this instead of
main.cc alone, so they see the same code wherever main.cc keeps it.
"""
import re
from pathlib import Path

MAIN = "native/host/main.cc"
# Files main.cc includes textually that hold main()'s code: the session, then the fragments it
# includes in its body, in include order.
SESSION_FILES = (
    "native/host/app/WorkspaceSession.inc",
    "native/host/app/WorkspaceState.inc",
    "native/host/app/WorkspaceViewport.inc",
    "native/host/app/WorkspaceStrokes.inc",
    "native/host/app/WorkspaceResources.inc",
    "native/host/app/WorkspaceCallbacks.inc",
    "native/host/app/WorkspacePaths.inc",
    "native/host/app/WorkspaceProject.inc",
    "native/host/app/WorkspaceMenus.inc",
    "native/host/app/WorkspaceAutomation.inc",
    "native/host/app/WorkspaceWindow.inc",
    "native/harness/SessionDemoFlags.inc",
    "native/harness/SessionDemoSequence.inc",
    "native/harness/SessionCaptureDemos.inc",
    "native/harness/SessionDemoActions.inc",
)


def main_source(root):
    root = Path(root)
    text = (root / MAIN).read_text()
    for rel in SESSION_FILES:
        line = f'#include "{rel.rsplit("/", 1)[1]}"\n'
        if text.count(line) != 1:
            raise AssertionError(f"{MAIN} must include {rel} exactly once")
        text = text.replace(line, (root / rel).read_text(), 1)
    return text


def host_rel(root, name):
    """'native/host/<folder>/<name>' for the one file called name anywhere below native/host.
    Host files live in domain folders and move between them; verifiers that need a host file
    by name resolve it here instead of writing 'native/host/' + name."""
    found = sorted(p for p in (Path(root) / "native/host").rglob(name) if p.is_file() and p.name == name)
    if len(found) != 1:
        raise AssertionError(f"expected one native/host file named {name}, found {len(found)}")
    return found[0].relative_to(Path(root)).as_posix()


def host_file(root, name):
    """The Path of the one file called name below native/host (see host_rel)."""
    return Path(root) / host_rel(root, name)


def host_rel_at(root, rev, name):
    """Where the host file called name was at commit rev (git ls-tree), for verifiers that
    compare a recorded snapshot's sources with today's."""
    import subprocess
    listing = subprocess.run(["git", "-C", str(root), "ls-tree", "-r", "--name-only", rev, "native/host"],
                             capture_output=True, text=True, check=True, timeout=120).stdout.split("\n")
    found = [p for p in listing if p.rsplit("/", 1)[-1] == name]
    if len(found) != 1:
        raise AssertionError(f"expected one native/host file named {name} at {rev}, found {len(found)}")
    return found[0]


def with_sections(root, rel):
    """rel's text with every `#include "Name.inc"` it holds replaced by that file, recursively.
    A big file such as native/host/MaterialInspection.cc keeps its code in .inc sections it
    includes in place; verifiers that read the file's code as text read this instead."""
    root = Path(root)
    found = {}
    for folder in ("native/host", "native/harness"):
        for path in (root / folder).rglob("*.inc"):
            found.setdefault(path.name, []).append(path)

    def expand(text, depth=0):
        def replace(match):
            paths = found.get(match.group(1), [])
            if len(paths) != 1 or depth > 8:
                return match.group(0)
            return expand(paths[0].read_text(), depth + 1)
        return re.sub(r'^#include "([A-Za-z0-9_]+\.inc)"\n', replace, text, flags=re.M)

    return expand((root / rel).read_text())


def compact(text):
    """text without any whitespace, so a needle matches however clang-format lays the code out.
    Write needles without spaces too: 'const auto save_document=' becomes 'constautosave_document='."""
    return re.sub(r"\s+", "", text)


_LITERAL_OR_COMMENT = re.compile(r'R"([^ ()\\\t\r\n]*)\([\s\S]*?\)\1"|"(?:\\.|[^"\\\n])*"|'
                                 r"'(?:\\.|[^'\\\n])*'|//[^\n]*|/\*[\s\S]*?\*/")


def statement(text, needle):
    """The statement that starts at the first `needle` in text: up to the ';' that ends it,
    counting (), [] and {} outside literals and comments. Use it to take one callback,
    for example statement(source, 'callbacks.create_smart_material=')."""
    start = text.index(needle)
    masked = _LITERAL_OR_COMMENT.sub(lambda m: " " * len(m[0]), text)
    depth = 0
    for index in range(start, len(text)):
        c = masked[index]
        if c in "([{":
            depth += 1
        elif c in ")]}":
            depth -= 1
        elif c == ";" and depth == 0:
            return text[start:index + 1]
    raise AssertionError(f"no end of statement after {needle!r}")
