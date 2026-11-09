#!/usr/bin/env python3
"""mp: mPainter's developer command (platform-neutral subset).

  mp version                 print the version from pyproject.toml
  mp version bump X.Y.Z      set the version in pyproject.toml and add a CHANGELOG.md heading
  mp check [--only NAME]     fast source gates; see CHECKS below
  mp doctor                  report which local tools are present, without changing anything
  mp brief --output PATH ... write a worker brief with every required field

Not here yet (later phases of the restructure plan): build, package, test, release.
Each status is PASS, FAIL, WARN or UNAVAILABLE. UNAVAILABLE never counts as a pass.
Exit status: 0 when nothing FAILs, 1 when a check FAILs, 2 for usage errors.
"""

import argparse
import os
import re
import shutil
import subprocess
import sys
import tomllib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TIMEOUT = 120
SEMVER = re.compile(r"(\d+)\.(\d+)\.(\d+)")
SOURCE_SUFFIXES = (".cc", ".hh", ".inl", ".c", ".cpp", ".h", ".hpp")
# Files that ship a version to users. Each must take it from pyproject.toml, not spell it.
VERSIONED_FILES = ("packaging/", "scripts/", "native/", "CMakeLists.txt")
VERSION_FIELD = re.compile(
    r"(?:CFBundle(?:Short)?Version(?:String)?</key>\s*<string>|\"(?:File|Product)Version\",\s*\"|"
    r"release version=\"|mPainter-)(\d+\.\d+\.\d+[\w.-]*)")
PINNED_CLANG = "21.1.8"


class UsageError(Exception):
    pass


def read_version(root):
    data = tomllib.loads((root / "pyproject.toml").read_text(encoding="utf-8"))
    version = data.get("project", {}).get("version")
    if not isinstance(version, str) or not SEMVER.fullmatch(version):
        raise UsageError("pyproject.toml has no [project] version of the form X.Y.Z")
    return version


def bump_version(root, new):
    old = read_version(root)
    if not SEMVER.fullmatch(new):
        raise UsageError(f"{new!r} is not X.Y.Z")
    if tuple(map(int, new.split("."))) <= tuple(map(int, old.split("."))):
        raise UsageError(f"{new} is not newer than {old}")
    pyproject = root / "pyproject.toml"
    text = pyproject.read_text(encoding="utf-8")
    line = re.compile(rf'(?m)^version\s*=\s*"{re.escape(old)}"\s*$')
    if len(line.findall(text)) != 1:
        raise UsageError('pyproject.toml must have exactly one line `version = "X.Y.Z"`')
    changelog = root / "CHANGELOG.md"
    log = changelog.read_text(encoding="utf-8") if changelog.exists() else "# Changelog\n"
    if re.search(rf"(?m)^## {re.escape(new)}\b", log):
        raise UsageError(f"CHANGELOG.md already has a {new} heading")
    title, _, rest = log.partition("\n")
    new_log = f"{title}\n\n## {new}\n\n" + rest.lstrip("\n")
    pyproject.write_text(line.sub(f'version = "{new}"', text), encoding="utf-8")
    changelog.write_text(new_log, encoding="utf-8")
    return old, new


def git_files(root):
    proc = subprocess.run(["git", "-C", str(root), "ls-files", "-z"], capture_output=True,
                          timeout=TIMEOUT, env=dict(os.environ, GIT_OPTIONAL_LOCKS="0"))
    if proc.returncode:
        raise UsageError("git ls-files failed; mp check needs a git worktree")
    paths = []
    for raw in proc.stdout.split(b"\0"):
        if raw:
            path = root / os.fsdecode(raw)
            if path.is_file() and not path.is_symlink():
                paths.append(os.fsdecode(raw))
    return paths


def text(root, rel):
    try:
        return (root / rel).read_text(encoding="utf-8")
    except (UnicodeDecodeError, OSError):
        return None


def grep(root, files, pattern):
    hits = []
    for rel in files:
        content = text(root, rel)
        if content is None:
            continue
        for number, line in enumerate(content.splitlines(), 1):
            if pattern.search(line):
                hits.append(f"{rel}:{number}")
    return hits


def check_version(root, files):
    version = read_version(root)
    spelled = re.compile(rf"(?<![\w.]){re.escape(version)}(?![\w.])")
    hits = grep(root, [f for f in files if f.startswith(VERSIONED_FILES) and f != "pyproject.toml"], spelled)
    for rel in files:
        if rel.startswith("packaging/") or rel.endswith(".py"):
            content = text(root, rel) or ""
            for match in VERSION_FIELD.finditer(content):
                hits.append(f"{rel}:{content.count(chr(10), 0, match.start()) + 1} ({match.group(1)})")
    hits = sorted(set(hits))
    return ("FAIL" if hits else "PASS", f"version {version} is spelled outside pyproject.toml", hits)


def check_quuid(root, files):
    pattern = re.compile(r"\bQUuid\s+\w+\s*=\s*\{\s*\}|\b\w*[uU]uid\w*\s*=\s*\{\s*\}")
    hits = grep(root, [f for f in files if f.endswith(SOURCE_SUFFIXES)], pattern)
    return ("FAIL" if hits else "PASS", "`QUuid x = {}` or `uuid = {}` does not compile on Windows; write QUuid()", hits)


def check_shared_ptr_unique(root, files):
    hits = grep(root, [f for f in files if f.endswith(SOURCE_SUFFIXES)], re.compile(r"\.unique\(\s*\)"))
    return ("FAIL" if hits else "PASS", "shared_ptr::unique() was removed in C++20; use use_count() == 1", hits)


def check_cmake_backslash(root, files):
    cmake = [f for f in files if f.endswith(("CMakeLists.txt", ".cmake"))]
    hits = grep(root, cmake, re.compile(r'"[A-Za-z]:\\|[\w)]\\[\w$]'))
    return ("FAIL" if hits else "PASS", "CMake paths must use /, never \\", hits)


def check_test_includes(root, files):
    pattern = re.compile(r'^\s*#\s*include\s*"(\.\./)*tests/|^\s*#\s*include\s*"[^"]*(Test|Fixture)\.hh"')
    production = [f for f in files if f.startswith("native/host/") and f.endswith(SOURCE_SUFFIXES)
                  and not re.search(r"(Test|Demo|Proof|Fixture)\w*\.(cc|hh)$", f)]
    hits = grep(root, production, pattern)
    return ("FAIL" if hits else "PASS", "production code must not include test headers", hits)


def blender_surface(root):
    """scripts/verify/blender_surface.py of the checked tree, or None."""
    path = root / "scripts/verify/blender_surface.py"
    if not path.is_file() or path.is_symlink():
        return None
    import importlib.util
    spec = importlib.util.spec_from_file_location("blender_surface", path)
    module = importlib.util.module_from_spec(spec)
    # mp check must not write into the tree it checks, not even a __pycache__.
    previous, sys.dont_write_bytecode = sys.dont_write_bytecode, True
    try:
        spec.loader.exec_module(module)
    finally:
        sys.dont_write_bytecode = previous
    return module


def check_blender_boundary(root, files):
    surface = blender_surface(root)
    if surface is None:
        return ("UNAVAILABLE", "scripts/verify/blender_surface.py is missing", [])
    _, _, includers = surface.classify(root)
    outside = sorted(f for f in includers if not f.startswith(surface.BOUNDARY))
    # Phase 5 of the plan moves these behind native/host/blender/. Until then this only reports.
    return ("WARN" if outside else "PASS",
            f"{len(outside)} files outside {surface.BOUNDARY} include Blender headers (enforced from phase 5)",
            outside)


def check_blender_surface_doc(root, files):
    surface = blender_surface(root)
    if surface is None:
        return ("UNAVAILABLE", "scripts/verify/blender_surface.py is missing", [])
    doc = root / "docs/blender-surface.md"
    if not doc.is_file():
        return ("UNAVAILABLE", "docs/blender-surface.md has not been generated yet", [])
    current = doc.read_text(encoding="utf-8") == surface.render(*surface.classify(root))
    return ("PASS" if current else "FAIL", "docs/blender-surface.md matches the includes" if current
            else "docs/blender-surface.md is stale; run scripts/verify/blender_surface.py --write", [])


def check_agent_docs(root, files):
    checker = root / "scripts/verify/check_agent_docs.py"
    if not checker.is_file():
        return ("UNAVAILABLE", "scripts/verify/check_agent_docs.py is missing", [])
    proc = subprocess.run([sys.executable, "-I", str(checker), "--root", str(root)], capture_output=True,
                          text=True, timeout=TIMEOUT)
    lines = proc.stdout.strip().splitlines()
    return ("PASS" if proc.returncode == 0 else "FAIL", lines[-1] if lines else "no output", lines[:-1])


def check_unpushed(root, files):
    proc = subprocess.run(["git", "-C", str(root), "rev-list", "--count", "@{upstream}..HEAD"],
                          capture_output=True, text=True, timeout=TIMEOUT,
                          env=dict(os.environ, GIT_OPTIONAL_LOCKS="0"))
    if proc.returncode:
        return ("UNAVAILABLE", "no upstream branch, so unpushed commits cannot be counted", [])
    count = int(proc.stdout.strip() or 0)
    return ("WARN" if count else "PASS", f"{count} commit(s) not pushed", [])


CHECKS = {
    "version": check_version,
    "quuid-reset": check_quuid,
    "shared-ptr-unique": check_shared_ptr_unique,
    "cmake-backslash": check_cmake_backslash,
    "test-includes": check_test_includes,
    "blender-boundary": check_blender_boundary,
    "blender-surface": check_blender_surface_doc,
    "agent-docs": check_agent_docs,
    "unpushed": check_unpushed,
}
NOT_YET = {
    "clang-format": "needs the format-only commit first; the format proof lives in the night run, not here",
    "blender-patch-count": "counting changed Blender files needs the split patches (phase 6)",
}


def run_checks(root, only=None):
    files = git_files(root)
    names = [only] if only else list(CHECKS)
    results = []
    for name in names:
        if name not in CHECKS:
            raise UsageError(f"unknown check {name!r}; known: {', '.join(CHECKS)}")
        status, summary, details = CHECKS[name](root, files)
        results.append((name, status, summary, details))
    return results


def doctor(root, which=shutil.which, run=subprocess.run):
    rows = []

    def probe(name, argv, required, expect=None):
        if not which(argv[0]):
            rows.append((name, "MISSING" if required else "UNAVAILABLE", f"{argv[0]} not on PATH"))
            return
        try:
            proc = run(argv, capture_output=True, text=True, timeout=TIMEOUT, stdin=subprocess.DEVNULL)
        except subprocess.TimeoutExpired:
            rows.append((name, "UNAVAILABLE", f"{' '.join(argv)} timed out"))
            return
        first = (proc.stdout or proc.stderr).strip().splitlines()[:1]
        detail = first[0] if first else f"exit {proc.returncode}"
        if proc.returncode:
            rows.append((name, "MISSING" if required else "UNAVAILABLE", f"{' '.join(argv)}: exit {proc.returncode}"))
        elif expect and expect not in detail:
            rows.append((name, "WRONG", f"{detail} (want {expect})"))
        else:
            rows.append((name, "OK", detail))

    requires = tomllib.loads((root / "pyproject.toml").read_text(encoding="utf-8"))["project"]["requires-python"]
    minimum = tuple(int(x) for x in re.findall(r"\d+", requires)[:2])
    rows.append(("python", "OK" if sys.version_info[:2] >= minimum else "WRONG",
                 f"{sys.version.split()[0]} (pyproject requires {requires})"))
    probe("git", ["git", "--version"], True)
    probe("cmake", ["cmake", "--version"], False)
    probe("distrobox", ["distrobox", "--version"], False)
    if which("distrobox"):
        probe("clang-format in bts-build", ["distrobox", "enter", "bts-build", "--", "clang-format", "--version"],
              False, PINNED_CLANG)
    else:
        probe("clang-format", ["clang-format", "--version"], False, PINNED_CLANG)
    return rows


def coder_never_list(root):
    guide = root / "docs/agents/coder.md"
    if not guide.is_file():
        raise UsageError("docs/agents/coder.md is missing, so the brief has no Never list to copy")
    match = re.search(r"(?ms)^## Never\n(.*?)(?=^## |\Z)", guide.read_text(encoding="utf-8"))
    items = [line for line in (match.group(1).splitlines() if match else []) if line.startswith("- ")]
    if not items:
        raise UsageError("docs/agents/coder.md has no `## Never` list")
    return items


def write_brief(root, args):
    fields = {"goal": args.goal, "platform": args.platform, "base": args.base, "worktree": args.worktree,
              "paths": args.paths, "proof": args.proof, "report": args.report}
    missing = [name for name, value in fields.items() if not value]
    if missing:
        raise UsageError(f"a brief needs every field; missing: {', '.join(missing)}")
    if not re.fullmatch(r"[0-9a-f]{40}", args.base):
        raise UsageError("--base must be a full 40-character commit SHA")
    output = Path(args.output)
    never = coder_never_list(root)
    body = [
        f"# Brief: {args.goal}", "",
        f"Goal: {args.goal}",
        f"Platform and machine: {args.platform}",
        f"Base commit: {args.base}",
        f"Worktree: {args.worktree}", "",
        "Paths the worker may write:", *[f"- `{p}`" for p in args.paths], "",
        "Forbidden actions (from docs/agents/coder.md, Never):", *never,
        *[f"- {item}" for item in args.forbid], "",
        f"Proof: {args.proof}",
        f"Report: {args.report}", "",
    ]
    with open(output, "x", encoding="utf-8") as handle:
        handle.write("\n".join(body))
    return output


def main(argv=None, root=ROOT, out=sys.stdout):
    parser = argparse.ArgumentParser(prog="mp", description=__doc__.split("\n\n")[0])
    sub = parser.add_subparsers(dest="command", required=True)
    version = sub.add_parser("version")
    version.add_argument("action", nargs="?", choices=["bump"])
    version.add_argument("value", nargs="?")
    check = sub.add_parser("check")
    check.add_argument("--only")
    sub.add_parser("doctor")
    brief = sub.add_parser("brief")
    for name in ("output", "goal", "platform", "base", "worktree", "proof", "report"):
        brief.add_argument(f"--{name}", required=name == "output", default="")
    brief.add_argument("--paths", action="append", default=[])
    brief.add_argument("--forbid", action="append", default=[])
    args = parser.parse_args(argv)
    try:
        if args.command == "version":
            if args.action == "bump":
                if not args.value:
                    raise UsageError("mp version bump needs X.Y.Z")
                old, new = bump_version(root, args.value)
                print(f"version {old} -> {new}; CHANGELOG.md has a {new} heading", file=out)
            else:
                print(read_version(root), file=out)
            return 0
        if args.command == "check":
            results = run_checks(root, args.only)
            for name, status, summary, details in results:
                print(f"{status:11} {name}: {summary}", file=out)
                for line in details[:50]:
                    print(f"            {line}", file=out)
                if len(details) > 50:
                    print(f"            ... {len(details) - 50} more", file=out)
            if not args.only:
                for name, reason in NOT_YET.items():
                    print(f"{'NOT YET':11} {name}: {reason}", file=out)
            return 1 if any(status == "FAIL" for _, status, _, _ in results) else 0
        if args.command == "doctor":
            rows = doctor(root)
            for name, status, detail in rows:
                print(f"{status:11} {name}: {detail}", file=out)
            return 1 if any(status in ("MISSING", "WRONG") for _, status, _ in rows) else 0
        if args.command == "brief":
            print(f"wrote {write_brief(root, args)}", file=out)
            return 0
    except (UsageError, FileExistsError) as error:
        print(f"mp: {error}", file=sys.stderr)
        return 2
    return 2


if __name__ == "__main__":
    sys.exit(main())
