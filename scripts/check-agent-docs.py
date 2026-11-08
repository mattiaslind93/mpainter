#!/usr/bin/env python3
"""Fail when an AGENTS.md names a path that does not exist, or a CLAUDE.md is more than its import line."""
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
SKIP_DIRS = {".git", "build", "node_modules"}
IMPORT_LINE = "@AGENTS.md\n"
PATH_SUFFIX = re.compile(r"\.(md|py|sh|cc|hh|h|inc|json|toml|patch|txt|ps1|yml|plist|xml|rc|cmake)$")
NOT_A_PATH = re.compile(r"[\s<>*${}()=#:~,@]|^\.\w+$")
CODE_SPAN = re.compile(r"`([^`\n]+)`")
FENCE = re.compile(r"^```.*?^```", re.M | re.S)


def doc_files(name):
    for path in ROOT.rglob(name):
        if not SKIP_DIRS.intersection(path.relative_to(ROOT).parts):
            yield path


def path_tokens(text):
    for token in CODE_SPAN.findall(FENCE.sub("", text)):
        if NOT_A_PATH.search(token):
            continue
        if "/" in token or PATH_SUFFIX.search(token):
            yield token


def exists(token, folder):
    candidates = [ROOT / token, folder / token]
    if any(c.exists() for c in candidates):
        return True
    if "/" not in token.rstrip("/"):
        return any(True for _ in folder.rglob(token.rstrip("/")))
    return False


def main():
    problems = []
    for agents in sorted(doc_files("AGENTS.md")):
        folder = agents.parent
        rel = agents.relative_to(ROOT)
        for token in path_tokens(agents.read_text(encoding="utf-8")):
            if not exists(token, folder):
                problems.append(f"{rel}: `{token}` does not exist")
        claude = folder / "CLAUDE.md"
        if not claude.exists():
            problems.append(f"{rel}: no CLAUDE.md beside it")
    for claude in sorted(doc_files("CLAUDE.md")):
        if claude.read_text(encoding="utf-8") != IMPORT_LINE:
            problems.append(f"{claude.relative_to(ROOT)}: must contain only {IMPORT_LINE.strip()}")
        if not (claude.parent / "AGENTS.md").exists():
            problems.append(f"{claude.relative_to(ROOT)}: no AGENTS.md beside it")
    for line in problems:
        print(line)
    print(f"check-agent-docs: {len(problems)} problem(s)")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
