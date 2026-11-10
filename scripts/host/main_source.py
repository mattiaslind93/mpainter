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
