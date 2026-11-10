#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""The source of mPainter's main() as one text, for verifiers that read it.

main.cc includes the Painter session from native/host/app/ (one translation unit, see
WorkspaceSession.inc). Verifiers that check main()'s callbacks as text read this instead of
main.cc alone, so they see the same code wherever main.cc keeps it.
"""
from pathlib import Path

MAIN = "native/host/main.cc"
# Files main.cc includes textually that hold main()'s code, in include order.
SESSION_FILES = ("native/host/app/WorkspaceSession.inc",)


def main_source(root):
    root = Path(root)
    text = (root / MAIN).read_text()
    for rel in SESSION_FILES:
        line = f'#include "{rel.rsplit("/", 1)[1]}"\n'
        if text.count(line) != 1:
            raise AssertionError(f"{MAIN} must include {rel} exactly once")
        text = text.replace(line, (root / rel).read_text(), 1)
    return text
