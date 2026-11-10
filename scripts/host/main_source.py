#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""The source of mPainter's main() as one text, for verifiers that read it.

main.cc includes the Painter session from native/host/app/ (one translation unit, see
WorkspaceSession.inc). Verifiers that check main()'s callbacks as text read this instead of
main.cc alone, so they see the same code wherever main.cc keeps it.
"""
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
