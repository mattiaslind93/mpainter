#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""mp: mPainter's developer command (platform-neutral subset).

  mp version                 print the version from pyproject.toml
  mp version bump X.Y.Z      set the version in pyproject.toml and add a CHANGELOG.md heading
  mp check [--only NAME]     fast source gates; see CHECKS below
  mp doctor                  report which local tools are present, without changing anything
  mp docs [--check]          regenerate docs/blender-surface.md and docs/architecture/bts-variables.md
  mp spdx [--write]          list (or add) missing SPDX-License-Identifier lines
  mp move DEST --list FILE [--refs]
                             git mv native/host files into a domain folder and repoint CMake
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
TOOL_TESTS = ("scripts/tests/", "scripts/verify/tests/")


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
    # The tests of mp itself spell versions as fixtures; nothing ships from there.
    files = [f for f in files if not f.startswith(TOOL_TESTS)]
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
    return verify_module(root, "blender_surface")


def verify_module(root, name):
    """scripts/verify/<name>.py of the checked tree, or None."""
    path = root / f"scripts/verify/{name}.py"
    if not path.is_file() or path.is_symlink():
        return None
    import importlib.util
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    # mp check must not write into the tree it checks, not even a __pycache__.
    previous, sys.dont_write_bytecode = sys.dont_write_bytecode, True
    try:
        spec.loader.exec_module(module)
    finally:
        sys.dont_write_bytecode = previous
    return module


RATCHETS = "scripts/verify/blender-ratchets.json"


def ratchet(root, key):
    """The recorded ceiling for key, or None when no baseline is committed."""
    import json
    path = root / RATCHETS
    if not path.is_file():
        return None
    value = json.loads(path.read_text(encoding="utf-8")).get(key)
    return value if isinstance(value, int) else None


def check_blender_boundary(root, files):
    surface = blender_surface(root)
    if surface is None:
        return ("UNAVAILABLE", "scripts/verify/blender_surface.py is missing", [])
    _, _, includers = surface.classify(root)
    outside = sorted(f for f in includers if not f.startswith(surface.BOUNDARY))
    ceiling = ratchet(root, "files_including_blender_outside_boundary")
    summary = f"{len(outside)} files outside {surface.BOUNDARY} include Blender headers"
    if ceiling is None:
        return ("UNAVAILABLE", summary + f"; no ceiling in {RATCHETS}", outside)
    # The plan moves them all behind native/host/blender/. Until then the count may only go down.
    if len(outside) > ceiling:
        return ("FAIL", summary + f", more than the recorded {ceiling}; include Blender only in "
                f"{surface.BOUNDARY}", outside)
    return ("WARN" if outside else "PASS", summary + f" (ceiling {ceiling}; lower it when it drops)", outside)


def blender_patch_files(root, files):
    """Blender files the numbered patches change, and files they add, from the diff headers."""
    changed, added = set(), set()
    for rel in sorted(f for f in files if f.startswith("third_party/blender/patches/") and f.endswith(".patch")):
        current = None
        for line in (text(root, rel) or "").splitlines():
            match = re.match(r"diff --git a/(\S+) b/(\S+)", line)
            if match:
                current = match.group(2)
                changed.add(current)
            elif current and line.startswith("new file mode"):
                changed.discard(current)
                added.add(current)
    return sorted(changed - added), sorted(added)


def check_blender_patch_count(root, files):
    changed, added = blender_patch_files(root, files)
    ceiling = ratchet(root, "changed_blender_files")
    summary = f"the patches change {len(changed)} Blender files and add {len(added)}"
    if ceiling is None:
        return ("UNAVAILABLE", summary + f"; no ceiling in {RATCHETS}", [])
    if len(changed) > ceiling:
        return ("FAIL", summary + f", more than the recorded {ceiling}", changed)
    return ("PASS", summary + f" (ceiling {ceiling}; lower it when it drops)", [])


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


# Generated pages: path, generator module, and how to render from that module.
GENERATED_DOCS = {
    "docs/blender-surface.md": ("blender_surface", lambda m, root: m.render(*m.classify(root))),
    "docs/architecture/bts-variables.md": ("bts_variables", lambda m, root: m.render(m.classify(root))),
}


def generated_docs(root):
    """{path: expected text or None when its generator is missing}"""
    result = {}
    for rel, (name, render) in GENERATED_DOCS.items():
        module = verify_module(root, name)
        result[rel] = None if module is None else render(module, root)
    return result


def check_bts_variables_doc(root, files):
    expected = generated_docs(root)["docs/architecture/bts-variables.md"]
    doc = root / "docs/architecture/bts-variables.md"
    if expected is None:
        return ("UNAVAILABLE", "scripts/verify/bts_variables.py is missing", [])
    if not doc.is_file():
        return ("UNAVAILABLE", "docs/architecture/bts-variables.md has not been generated yet", [])
    current = doc.read_text(encoding="utf-8") == expected
    return ("PASS" if current else "FAIL", "docs/architecture/bts-variables.md matches the code" if current
            else "docs/architecture/bts-variables.md is stale; run python3 scripts/mp.py docs", [])


def write_docs(root, check_only):
    stale = []
    for rel, text in generated_docs(root).items():
        if text is None:
            raise UsageError(f"the generator for {rel} is missing")
        path = root / rel
        if path.is_symlink() or any(p.is_symlink() for p in path.parents if root in p.parents):
            raise UsageError(f"{rel} goes through a symlink")
        if path.is_file() and path.read_text(encoding="utf-8") == text:
            continue
        stale.append(rel)
        if not check_only:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(text, encoding="utf-8")
    return stale


HOST = "native/host"
HOST_CMAKE = "native/host/CMakeLists.txt"
MOVE_DESTINATIONS = re.compile(r"native/(harness|host/[a-z][a-z0-9_-]*(/[a-z][a-z0-9_-]*)?)\Z")
REF_FILES = re.compile(r"(scripts|native)/.*\.(py|json|sh)\Z|native/tests/.*\.(cc|hh|inc)\Z")
NAME_END = r"(?![A-Za-z0-9_.])"
# Standalone CMake projects that compile host sources by relative path.
TEST_CMAKE = re.compile(r"native/tests/[^/]+/CMakeLists\.txt")


def name_references(root, names):
    """Where scripts, JSON, shell files or tests name these files: by file name, or by a quoted
    bare stem (verifiers build paths as 'native/host/' + stem + '.cc'). [(file:line, name)]"""
    stems = {n.split(".")[0]: n for n in names}
    alternatives = "|".join(re.escape(n) for n in names)
    full = re.compile(r"(?<![A-Za-z0-9_])(" + alternatives + r")(?![A-Za-z0-9_.])")
    # In C++ a bare include resolves through the include path; only a relative path pins a folder.
    relative = re.compile(r'"\.\./host/(' + alternatives + r')"')
    quoted = re.compile(r"""['"]([A-Za-z0-9_]+)['"]""")
    hits = []
    for rel in git_files(root):
        if not REF_FILES.match(rel) or rel == HOST_CMAKE:
            continue
        content = text(root, rel) or ""
        pattern = relative if rel.endswith((".cc", ".hh", ".inc")) else full
        for number, line in enumerate(content.splitlines(), 1):
            for match in pattern.finditer(line):
                hits.append((f"{rel}:{number}", match.group(1)))
            if rel.endswith(".py"):
                for stem in quoted.findall(line):
                    if stem in stems:
                        hits.append((f"{rel}:{number}", stems[stem]))
    return hits


def move_files(root, dest, sources, refs=False, allow_referenced=False):
    """git mv top-level native/host files into dest and repoint what names them.

    Rewrites `${BTS_HOST_SOURCE_DIR}/<name>` in native/host/CMakeLists.txt and
    `../../host/<name>` in the standalone projects (native/tests/*/CMakeLists.txt). With
    refs, also `native/host/<name>` in tracked Python, JSON and shell files and
    `"../host/<name>"` includes in native/tests. Adding dest to BTS_HOST_DOMAINS in
    native/host/SourceIncludeDirs.cmake is left to the caller, because it is a decision.
    Returns {"cmake": n, "refs": {file: n}}.
    """
    if not MOVE_DESTINATIONS.fullmatch(dest):
        raise UsageError(f"{dest} is not native/harness or a native/host domain folder")
    tracked = set(git_files(root))
    names = []
    for rel in sources:
        name = rel[len(HOST) + 1:]
        if not rel.startswith(HOST + "/") or "/" in name or rel not in tracked:
            raise UsageError(f"{rel} is not a tracked top-level native/host file")
        if (root / dest / name).exists():
            raise UsageError(f"{dest}/{name} already exists")
        names.append(name)
    if len(set(names)) != len(names):
        raise UsageError("a file is listed twice")
    if not allow_referenced:
        hits = name_references(root, names)
        if hits:
            shown = "\n  ".join(f"{where}: {name}" for where, name in hits[:30])
            raise UsageError(f"{len(hits)} reference(s) name these files; leave them out, or check each one "
                             f"and pass --allow-referenced (with --refs for exact paths):\n  {shown}")
    prefix = os.path.relpath(dest, HOST).replace(os.sep, "/")
    cmake = (root / HOST_CMAKE).read_text(encoding="utf-8")
    total = 0
    for name in names:
        pattern = re.compile(r"\$\{BTS_HOST_SOURCE_DIR\}/" + re.escape(name) + NAME_END)
        cmake, n = pattern.subn(lambda m, name=name: "${BTS_HOST_SOURCE_DIR}/" + prefix + "/" + name, cmake)
        total += n
    rewritten = {}
    for rel in sorted(r for r in tracked if TEST_CMAKE.fullmatch(r)):
        content = (root / rel).read_text(encoding="utf-8")
        count = 0
        for name in names:
            pattern = re.compile(r"\.\./\.\./host/" + re.escape(name) + NAME_END)
            content, n = pattern.subn(lambda m, name=name: f"../../host/{prefix}/{name}", content)
            count += n
        if count:
            (root / rel).write_text(content, encoding="utf-8")
            total += count
    if refs:
        rules = [(re.compile(r"native/host/" + re.escape(n) + NAME_END), f"{dest}/{n}") for n in names]
        rules += [(re.compile(r'"\.\./host/' + re.escape(n) + '"'), f'"../{dest[len("native/"):]}/{n}"')
                  for n in names]
        for rel in sorted(tracked):
            path = root / rel
            if not REF_FILES.match(rel) or path.is_symlink() or not path.is_file():
                continue
            content = path.read_text(encoding="utf-8", errors="surrogateescape")
            count = 0
            for pattern, replacement in rules:
                content, n = pattern.subn(lambda m, r=replacement: r, content)
                count += n
            if count:
                path.write_text(content, encoding="utf-8", errors="surrogateescape")
                rewritten[rel] = count
    (root / dest).mkdir(parents=True, exist_ok=True)
    for rel, name in zip(sources, names):
        proc = subprocess.run(["git", "-C", str(root), "mv", "--", rel, f"{dest}/{name}"], capture_output=True,
                              text=True, timeout=TIMEOUT, stdin=subprocess.DEVNULL)
        if proc.returncode:
            raise UsageError(f"git mv {rel} failed: {proc.stderr.strip()}")
    (root / HOST_CMAKE).write_text(cmake, encoding="utf-8")
    return {"cmake": total, "refs": rewritten}


SPDX_LICENSE = "GPL-3.0-or-later"
SPDX_PATTERN = re.compile(r"SPDX-License-Identifier:")
SPDX_FOLDERS = ("native/", "scripts/")
SPDX_COMMENTS = {".cc": "//", ".hh": "//", ".h": "//", ".c": "//", ".cpp": "//", ".hpp": "//", ".inl": "//",
                 ".inc": "//", ".mm": "//", ".py": "#", ".sh": "#", ".cmake": "#", ".ps1": "#", ".php": "//"}


def spdx_comment(rel):
    if rel.endswith("/CMakeLists.txt") or rel == "CMakeLists.txt":
        return "#"
    stem, suffix = os.path.splitext(rel)
    if suffix == ".in":  # configure_file() templates take the comment style of what they generate
        stem, suffix = os.path.splitext(stem)
    return SPDX_COMMENTS.get(suffix)


def pinned_hashes(root, files):
    """(hashes, paths) pinned in tracked Python or JSON files: every 64-hex value, and every
    quoted native/ or scripts/ path on a line that also holds one. A pin may hash a transformed
    copy of its file (cycles_burst_snapshot.py strips a friend line first), so the path counts too."""
    hashes, paths = set(), set()
    for rel in files:
        if rel.endswith((".py", ".json")) and rel.startswith(SPDX_FOLDERS):
            for line in (text(root, rel) or "").splitlines():
                found = re.findall(r"\b[0-9a-f]{64}\b", line)
                if found:
                    hashes.update(found)
                    paths.update(re.findall(r"""['"]((?:native|scripts)/[^'"\s]+)['"]""", line))
    return hashes, paths


def spdx_plan(root):
    """(missing, pinned, present): source files without an SPDX line, those left alone because a
    verifier pins their sha256, and those that already carry one (any licence)."""
    import hashlib
    files = git_files(root)
    pins, pinned_paths = pinned_hashes(root, files)
    missing, pinned, present = [], [], []
    for rel in files:
        if not rel.startswith(SPDX_FOLDERS) or spdx_comment(rel) is None:
            continue
        data = (root / rel).read_bytes()
        head = "\n".join(data.decode("utf-8", "replace").split("\n")[:3])
        if SPDX_PATTERN.search(head):
            present.append(rel)
        elif hashlib.sha256(data).hexdigest() in pins or rel in pinned_paths:
            pinned.append(rel)
        else:
            missing.append(rel)
    return missing, pinned, present


def add_spdx(path, comment):
    data = path.read_text(encoding="utf-8", errors="surrogateescape")
    line = f"{comment} SPDX-License-Identifier: {SPDX_LICENSE}\n"
    # The line goes after an interpreter line: a shebang, or PHP's opening tag.
    if data.startswith("#!") or data.startswith("<?php\n"):
        first, _, rest = data.partition("\n")
        path.write_text(first + "\n" + line + rest, encoding="utf-8", errors="surrogateescape")
    else:
        path.write_text(line + data, encoding="utf-8", errors="surrogateescape")


def check_test_cmake_paths(root, files):
    """Every relative source path and included file in the standalone native/tests/*/ projects
    exists. Nothing builds them here, so a move that misses one would go unnoticed."""
    hits = []
    for rel in files:
        if not TEST_CMAKE.fullmatch(rel):
            continue
        folder = (root / rel).parent
        for number, line in enumerate((text(root, rel) or "").splitlines(), 1):
            for path in re.findall(r"(?<![\w${}/.-])(\.\./[\w./-]+\.(?:cc|cpp|c|hh|h|cmake))(?![\w.])", line):
                if not (folder / path).is_file():
                    hits.append(f"{rel}:{number}: {path}")
    return ("FAIL" if hits else "PASS", "standalone test projects name only existing sources", hits)


def check_spdx(root, files):
    missing, pinned, _ = spdx_plan(root)
    summary = (f"{len(missing)} source files under native/ and scripts/ lack an SPDX line; "
               f"{len(pinned)} more are left alone because a verifier pins their sha256")
    return ("FAIL" if missing else "PASS", summary, missing + [f"pinned: {p}" for p in pinned])


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
    "test-cmake-paths": check_test_cmake_paths,
    "test-includes": check_test_includes,
    "blender-boundary": check_blender_boundary,
    "blender-surface": check_blender_surface_doc,
    "bts-variables": check_bts_variables_doc,
    "blender-patch-count": check_blender_patch_count,
    "spdx": check_spdx,
    "agent-docs": check_agent_docs,
    "unpushed": check_unpushed,
}
NOT_YET = {
    "clang-format": "not a gate yet: 442 owned files stay unformatted until the verifiers that pin their "
                    "text or hash change with them",
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
    docs = sub.add_parser("docs")
    docs.add_argument("--check", action="store_true")
    spdx = sub.add_parser("spdx")
    spdx.add_argument("--write", action="store_true", help=f"add `SPDX-License-Identifier: {SPDX_LICENSE}`")
    move = sub.add_parser("move")
    move.add_argument("dest")
    move.add_argument("--list", required=True, help="file with one native/host/<file> path per line")
    move.add_argument("--refs", action="store_true", help="also rewrite exact paths in scripts and tests")
    move.add_argument("--allow-referenced", action="store_true",
                      help="move files that scripts or tests name (checked by hand)")
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
        if args.command == "docs":
            stale = write_docs(root, args.check)
            for rel in stale:
                print(f"{'stale' if args.check else 'wrote'} {rel}", file=out)
            if not stale:
                print("generated docs are current", file=out)
            return 1 if args.check and stale else 0
        if args.command == "spdx":
            missing, pinned, present = spdx_plan(root)
            if args.write:
                for rel in missing:
                    add_spdx(root / rel, spdx_comment(rel))
            verb = "added" if args.write else "missing"
            print(f"{verb}: {len(missing)}; pinned by a verifier, left alone: {len(pinned)}; "
                  f"already present: {len(present)}", file=out)
            for rel in pinned:
                print(f"  pinned {rel}", file=out)
            return 0 if args.write or not missing else 1
        if args.command == "move":
            listing = Path(args.list)
            sources = [line.strip() for line in listing.read_text(encoding="utf-8").splitlines() if line.strip()]
            result = move_files(root, args.dest, sources, args.refs, args.allow_referenced)
            print(f"moved {len(sources)} files to {args.dest}; {result['cmake']} CMake references", file=out)
            for rel, count in result["refs"].items():
                print(f"  rewrote {count} reference(s) in {rel}", file=out)
            return 0
        if args.command == "brief":
            print(f"wrote {write_brief(root, args)}", file=out)
            return 0
    except (UsageError, FileExistsError) as error:
        print(f"mp: {error}", file=sys.stderr)
        return 2
    return 2


if __name__ == "__main__":
    sys.exit(main())
