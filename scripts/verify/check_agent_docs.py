#!/usr/bin/env python3
"""Check that the paths and commands named in every AGENTS.md exist, and that every CLAUDE.md is only `@AGENTS.md`.

The documents are data. This script reads them and looks up files and CMake targets.
It never runs a command it finds in a document.

Only code spans and lines in shell fences are references. A reference passes only when
its file, script or CMake target is found. Anything the script cannot check is listed
under "not checked" with the reason, and never counts as a pass.

How a path is looked up:
  1. relative to the folder of the AGENTS.md, then relative to the repository root;
  2. a bare file name with no "/" that is in neither place passes only if exactly one
     file of that name exists below the AGENTS.md folder. Several matches are ambiguous
     and not checked.
A path that leaves the root, through ".." or a symlink, is not checked and is never read.
The lookup sees the filesystem, not git: an untracked or ignored file counts as present.

CMake targets are names declared by add_custom_target, add_executable or add_library in a
CMakeLists.txt or *.cmake file, outside comments. Every name after --target or -t in a
`cmake --build` command is checked. Targets declared through variables, inside Blender
patches or in disabled if() blocks are not understood.
"""
import argparse
import os
import pathlib
import re
import shlex
import sys

SKIP_DIRS = {".git", "build", "node_modules", "__pycache__"}
IMPORT_LINE = "@AGENTS.md\n"
SHELL_FENCES = {"sh", "bash", "shell", "console", "zsh"}
PATH_SUFFIX = re.compile(r"\.(md|py|sh|cc|hh|h|inc|json|toml|patch|txt|ps1|yml|yaml|plist|xml|rc|cmake)$")
CODE_SPAN = re.compile(r"`([^`\n]+)`")
FENCE = re.compile(r"^\s*```\s*([\w+-]*)")
PLACEHOLDER = re.compile(r"<[^<>]*>|\$\{?\w|\.\.\.|[*?{}\[\]]")
SHELL_OPERATOR = re.compile(r"&&|\|\||[;|<>`]|\$\(")
NOT_A_PATH = re.compile(r"[\s()=#,@]|::|^\.\w+$")
CMAKE_TARGET = re.compile(r"\badd_(?:custom_target|executable|library)\s*\(\s*([\w.+-]+)")
CMAKE_BRACKET_COMMENT = re.compile(r"#\[(=*)\[.*?\]\1\]", re.S)
ENV_ASSIGNMENT = re.compile(r"^[A-Za-z_]\w*=")
PYTHONS = {"python", "python3", "py"}
SHELLS = {"source", ".", "sh", "bash"}
OUTSIDE = "resolves outside the repository"


class Report:
    def __init__(self):
        self.problems = []
        self.checked = 0
        self.unchecked = []

    def problem(self, where, message):
        self.problems.append(f"{where}: {message}")

    def ok(self):
        self.checked += 1

    def skip(self, where, token, reason):
        self.unchecked.append((reason, f"{where}: `{token}`"))


def cmake_code(text):
    """CMake text with line and bracket comments removed. A `#` inside a quoted argument is kept."""
    text = CMAKE_BRACKET_COMMENT.sub(" ", text)
    out, quoted, i = [], False, 0
    while i < len(text):
        char = text[i]
        if char == "\\" and quoted:
            out.append(text[i : i + 2])
            i += 2
            continue
        if char == '"':
            quoted = not quoted
        elif char == "#" and not quoted:
            end = text.find("\n", i)
            i = len(text) if end < 0 else end
            continue
        out.append(char)
        i += 1
    return "".join(out)


class Tree:
    """Read-only view of the repository: file names and CMake targets, collected once.

    A symlink whose target is outside the root is kept out of the view and listed in `escaped`.
    """

    def __init__(self, root):
        self.root = root
        self.files = []
        self.escaped = []
        for folder, dirs, names in os.walk(root):
            dirs[:] = sorted(d for d in dirs if d not in SKIP_DIRS)
            base = pathlib.Path(folder)
            for entry in [base / d for d in dirs] + [base / n for n in sorted(names)]:
                if entry.is_symlink() and not self.contains(entry.resolve()):
                    self.escaped.append(entry)
                else:
                    self.files.append(entry)
        self._targets = None

    def contains(self, path):
        return path == self.root or self.root in path.parents

    def find(self, name):
        return [p for p in self.files if p.name == name]

    def locate(self, token, folder):
        """Return "found", "missing" or "outside" for a path relative to `folder`, then to the root."""
        relative = token.rstrip("/")
        inside = False
        for base in (folder, self.root):
            lexical = pathlib.Path(os.path.normpath(base / relative))
            if not self.contains(lexical) or not self.contains(lexical.resolve()):
                continue
            inside = True
            if lexical.exists():
                return "found"
        return "missing" if inside else "outside"

    def targets(self):
        if self._targets is None:
            self._targets = set()
            for path in self.files:
                if path.name == "CMakeLists.txt" or path.suffix == ".cmake":
                    text = path.read_text(encoding="utf-8", errors="replace")
                    self._targets.update(CMAKE_TARGET.findall(cmake_code(text)))
        return self._targets


def references(text):
    """Yield (line number, token, kind): kind is "span", "shell" (a shell-fence line) or "fence" (any other fence)."""
    fence = None
    for number, line in enumerate(text.splitlines(), 1):
        opening = FENCE.match(line)
        if opening:
            fence = None if fence is not None else opening.group(1).lower()
            if fence is not None and fence not in SHELL_FENCES:
                yield number, line.strip(), "fence"
            continue
        if fence is None:
            for token in CODE_SPAN.findall(line):
                yield number, token.strip(), "span"
        elif fence in SHELL_FENCES:
            command = line.strip()
            if command.startswith("$ "):
                command = command[2:]
            if command and not command.startswith("#"):
                yield number, command, "shell"


def unreachable_reason(token):
    if "://" in token:
        return "URL"
    if "\\" in token or re.match(r"^[A-Za-z]:", token):
        return "platform-specific (Windows) path"
    if token.startswith(("/", "~")):
        return "outside the repository"
    if PLACEHOLDER.search(token):
        return "placeholder or pattern"
    return None


def check_path(token, where, folder, tree, report, by_name):
    reason = unreachable_reason(token)
    if reason:
        return report.skip(where, token, reason)
    found = tree.locate(token, folder)
    if found == "found":
        return report.ok()
    if found == "outside":
        return report.skip(where, token, OUTSIDE)
    name = token.rstrip("/")
    if by_name and "/" not in name:
        matches = [p for p in tree.find(name) if folder in p.parents]
        if len(matches) == 1:
            return report.ok()
        if len(matches) > 1:
            return report.skip(where, token, "ambiguous bare name")
    report.problem(where, f"`{token}` does not exist")


def build_targets(args):
    """Every name after --target or -t, up to the next option. None marks a --target with no name."""
    names, i = [], 0
    while i < len(args) and args[i] != "--":
        if args[i] in ("--target", "-t"):
            i += 1
            start = i
            while i < len(args) and not args[i].startswith("-"):
                names.append(args[i])
                i += 1
            if i == start:
                names.append(None)
            continue
        i += 1
    return names


def check_command(command, where, folder, tree, report):
    if SHELL_OPERATOR.search(re.sub(r"<[^<>]*>", "x", command)):  # `<build>` is a placeholder, not a redirect
        return report.skip(where, command, "compound shell command")
    try:
        words = shlex.split(command)
    except ValueError:
        return report.skip(where, command, "unparsable command")
    while words and ENV_ASSIGNMENT.match(words[0]):
        words.pop(0)
    if not words:
        return report.skip(where, command, "unsupported command")
    program, args = words[0], words[1:]
    if program in PYTHONS:
        if "-m" in args or "-c" in args:
            return report.skip(where, command, "python -m or -c")
        scripts = [a for a in args if not a.startswith("-")]
        if not scripts:
            return report.skip(where, command, "unsupported command")
        return check_path(scripts[0], where, folder, tree, report, by_name=False)
    if program in SHELLS and args:
        return check_path(args[0], where, folder, tree, report, by_name=False)
    if "/" in program:
        return check_path(program, where, folder, tree, report, by_name=False)
    if program == "cmake" and "--build" in args and ({"--target", "-t"} & set(args)):
        for target in build_targets(args):
            if target is None:
                report.problem(where, f"`{command}` has no target after --target")
            elif PLACEHOLDER.search(target):
                report.skip(where, target, "placeholder or pattern")
            elif target in tree.targets():
                report.ok()
            else:
                report.problem(where, f"CMake target `{target}` is not defined in any CMakeLists.txt")
        return
    report.skip(where, command, f"unsupported command `{program}`")


def check_agents(path, tree, report):
    folder = path.parent
    rel = path.relative_to(tree.root).as_posix()
    for number, token, kind in references(path.read_text(encoding="utf-8")):
        where = f"{rel}:{number}"
        shape = re.sub(r"<[^<>]*>", "x", token)
        if kind == "fence":
            report.skip(where, token, "fenced block that is not shell")
        elif kind == "shell" or re.search(r"\s", shape):
            check_command(token, where, folder, tree, report)
        elif token.startswith("<") and token.endswith(">") and "/" not in token:
            report.skip(where, token, "placeholder or pattern")
        elif NOT_A_PATH.search(shape):
            continue
        elif "/" in token or "\\" in token or PATH_SUFFIX.search(token):
            check_path(token, where, folder, tree, report, by_name=True)
    if not (folder / "CLAUDE.md").exists():
        report.problem(rel, "no CLAUDE.md beside it")


def check_claude(path, tree, report):
    rel = path.relative_to(tree.root).as_posix()
    if path.read_text(encoding="utf-8") != IMPORT_LINE:
        report.problem(rel, f"must contain only {IMPORT_LINE.strip()}")
    if not (path.parent / "AGENTS.md").exists():
        report.problem(rel, "no AGENTS.md beside it")


def run(root):
    tree = Tree(root)
    report = Report()
    for link in tree.escaped:
        report.skip(link.relative_to(root).as_posix(), os.readlink(link), "symlink that leaves the repository")
    agents = tree.find("AGENTS.md")
    if not agents:
        report.problem(root.as_posix(), "no AGENTS.md found, so nothing was checked")
    for path in agents:
        check_agents(path, tree, report)
    for path in tree.find("CLAUDE.md"):
        check_claude(path, tree, report)
    return report, len(agents)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument(
        "--root",
        type=pathlib.Path,
        default=pathlib.Path(__file__).resolve().parents[2],
        help="repository root to check (default: the repository that holds this script)",
    )
    parser.add_argument("--verbose", action="store_true", help="list every reference that was not checked")
    options = parser.parse_args(argv)
    root = options.root.resolve()
    if not root.is_dir():
        parser.error(f"--root {root} is not a directory")

    report, documents = run(root)
    for line in report.problems:
        print(line)
    reasons = {}
    for reason, item in report.unchecked:
        reasons.setdefault(reason, []).append(item)
    for reason in sorted(reasons):
        print(f"not checked ({reason}): {len(reasons[reason])}")
        if options.verbose:
            for item in reasons[reason]:
                print(f"  {item}")
    print(
        f"check_agent_docs: {documents} AGENTS.md, {report.checked} reference(s) found, "
        f"{len(report.unchecked)} not checked, {len(report.problems)} problem(s)"
    )
    return 1 if report.problems else 0


if __name__ == "__main__":
    sys.exit(main())
