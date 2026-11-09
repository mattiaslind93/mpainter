#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""List which tracked C/C++ files mPainter owns and may format.

The list comes from git, not from the disk: `git ls-tree -r -z` at one commit.
Each file with a C/C++ extension gets one status:

  include  owned by mPainter, a regular file, safe to hand to the format proof
  exclude  never formatted (vendor, generated, symlink, platform-specific, ...)
  review   ownership or content is unclear; a person decides. Never formatted.

A file is only included when an explicit rule below says mPainter owns it.
Unknown ownership is review, never include. The script only reads: it runs
read-only git commands, reads file bytes, and writes one new output file that
must not exist yet. It never runs or imports anything from the tree.

  python3 scripts/verify/format_owned_manifest.py --root . --output OUT.json
      [--base COMMIT] [--write-scope .night-run/write-scope.json]

Exit status: 0 written, 2 usage or safety error.
"""

import argparse
import functools
import hashlib
import json
import os
import re
import stat
import subprocess
import sys
from pathlib import Path

EXTENSIONS = (".cc", ".hh", ".inl", ".c", ".cpp", ".h", ".hpp", ".mm", ".m")
# C++ text that is pasted into another file by #include, often in the middle of a function.
# Listed so it is not silently missing, but never included: it cannot be proven on its own.
FRAGMENT_EXTENSIONS = (".inc", ".ipp", ".tcc", ".cxx", ".hxx")
GIT_TIMEOUT_SECONDS = 120
HEADER_BYTES = 4096

# Directories mPainter owns. A file under one of these is owned unless an
# exclusion below also matches. Order does not matter; there is no overlap.
OWNED_ROOTS = {
    "native/host/": "native-host",
    "native/harness/": "native-harness",
    "native/tests/": "native-tests",
    "native/spsm/": "native-spsm",
    "native/sbsar_metadata/": "native-sbsar-metadata",
}
# Single owned files outside those roots: platform-neutral probe programs.
OWNED_FILES = {
    "scripts/host/qt-probe/main.cc": "scripts-probe",
}
# Path components that mark code mPainter does not own or that is produced.
FOREIGN_COMPONENTS = {
    "third_party": "third-party",
    "vendor": "vendor",
    "external": "external",
    "extern": "external",
    "_deps": "external",
    "node_modules": "external",
    "generated": "generated",
    "build": "build-output",
    "dist": "build-output",
    ".cache": "build-output",
    ".git": "git-metadata",
}
# Platform-specific code stays out of a platform-neutral format pass.
PLATFORM_PREFIXES = ("scripts/cross-windows/", "packaging/")
PLATFORM_COMPONENTS = {"windows", "win32", "macos", "darwin", "mac", "ios"}
PLATFORM_EXTENSIONS = (".mm", ".m")

GENERATED_MARKER = re.compile(
    rb"(?i)(@generated|auto-?generated|generated (by|from|with)|do not edit|"
    rb"this file (is|was) generated|vendored|third[- ]party)"
)
# Only in a comment line, so code such as `count(c)` does not match.
COPYRIGHT_MARKER = re.compile(
    rb"(?im)^[ \t]*(//|/\*|\*)[^\n]*(\bcopyright\b|\bspdx-license-identifier\b|\(c\)[ \t]*\d{4})"
)
# A folder with its own licence file usually holds code someone else wrote.
LICENCE_FILE = re.compile(r"(?i)(licen[cs]e|copying|notice)(\.[a-z]+)?\Z")

RISK_PATTERNS = {
    "define": re.compile(rb"(?m)^[ \t]*#[ \t]*define\b"),
    "conditional": re.compile(rb"(?m)^[ \t]*#[ \t]*(if|ifdef|ifndef|elif)\b"),
    "includes-source-file": re.compile(rb'(?m)^[ \t]*#[ \t]*include[ \t]*"[^"\n]*\.(cc|cpp|c)"'),
    "using-declaration": re.compile(rb"(?m)^[ \t]*using[ \t]+(?!namespace\b)[A-Za-z_:][^=;\n]*;"),
    "raw-string": re.compile(rb'R"[^ ()\\\t\n]{0,16}\('),
    "line-splice": re.compile(rb"\\[ \t]*\r?\n"),
    "platform-conditional": re.compile(rb"\b(_WIN32|_WIN64|_MSC_VER|__APPLE__|__MACH__|__linux__)\b"),
}


class ManifestError(Exception):
    pass


def git(root, *args):
    """Run one read-only git command and return stdout bytes."""
    allowed = {"rev-parse", "ls-tree"}
    if args[0] not in allowed:
        raise ManifestError(f"git {args[0]} is not a read-only command this script uses")
    env = dict(os.environ, GIT_OPTIONAL_LOCKS="0", GIT_TERMINAL_PROMPT="0", LC_ALL="C")
    try:
        proc = subprocess.run(
            ["git", "-C", str(root), *args],
            stdin=subprocess.DEVNULL,
            capture_output=True,
            timeout=GIT_TIMEOUT_SECONDS,
            env=env,
            check=False,
        )
    except subprocess.TimeoutExpired:
        raise ManifestError(f"git {' '.join(args)} timed out")
    if proc.returncode:
        raise ManifestError(f"git {' '.join(args)} failed: {proc.stderr.decode(errors='replace').strip()}")
    return proc.stdout


def tree_entries(root, commit):
    """Yield (mode, type, object id, path bytes) for every entry at commit."""
    data = git(root, "ls-tree", "-r", "-z", "--full-tree", commit)
    for record in data.split(b"\0"):
        if not record:
            continue
        meta, _, path = record.partition(b"\t")
        mode, kind, oid = meta.split(b" ")
        yield mode.decode(), kind.decode(), oid.decode(), path


def blob_id(data, oid_length):
    """The git blob id of data, in the repository's object format."""
    header = b"blob %d\0" % len(data)
    algorithm = hashlib.sha1 if oid_length == 40 else hashlib.sha256
    return algorithm(header + data).hexdigest()


def ownership(path):
    for prefix, group in OWNED_ROOTS.items():
        if path.startswith(prefix):
            return group
    return OWNED_FILES.get(path)


def path_problems(raw):
    """Reasons a tracked path is unsafe to touch, from its bytes alone."""
    problems = []
    try:
        text = raw.decode("utf-8")
    except UnicodeDecodeError:
        return ["path is not UTF-8"]
    parts = text.split("/")
    if text.startswith("/") or any(part in ("", ".", "..") for part in parts):
        problems.append("path is absolute or has an empty, '.' or '..' component")
    if any(ord(c) < 0x20 or c == "\x7f" for c in text):
        problems.append("path has a control character")
    if "\\" in text:
        problems.append("path has a backslash")
    if any(part.startswith("-") for part in parts):
        problems.append("path component starts with '-'")
    return problems


def disk_problems(root, path):
    """Reasons the working-tree copy is unsafe, or None and the file bytes."""
    current = root
    for part in path.split("/")[:-1]:
        current = current / part
        try:
            info = os.lstat(current)
        except FileNotFoundError:
            return ["missing in the working tree"], None
        if stat.S_ISLNK(info.st_mode):
            return [f"parent {current.relative_to(root)} is a symlink"], None
        if not stat.S_ISDIR(info.st_mode):
            return [f"parent {current.relative_to(root)} is not a directory"], None
        if (current / ".git").exists() and current != root:
            return [f"{current.relative_to(root)} is a nested git checkout"], None
    target = root / path
    try:
        info = os.lstat(target)
    except FileNotFoundError:
        return ["missing in the working tree"], None
    if stat.S_ISLNK(info.st_mode):
        return ["symlink in the working tree"], None
    if not stat.S_ISREG(info.st_mode):
        return ["not a regular file in the working tree"], None
    if info.st_nlink != 1:
        return ["file has more than one hard link"], None
    resolved = target.resolve()
    if resolved != target or root not in resolved.parents:
        return ["resolves outside the root"], None
    with open(target, "rb") as handle:
        return [], handle.read()


@functools.lru_cache(maxsize=4096)
def folder_names(folder):
    return tuple(sorted(os.listdir(folder)))


def licence_folder(root, path):
    """The first licence file in the file's folder or a parent below the root."""
    folder = (root / path).parent
    while folder != root:
        for name in folder_names(folder):
            if LICENCE_FILE.match(name):
                return str((folder / name).relative_to(root))
        folder = folder.parent
    return None


def content_findings(data):
    """(exclude reasons, review reasons, risk tags) from the file bytes."""
    exclude, review = [], []
    if b"\0" in data:
        exclude.append("binary content (NUL byte)")
        return exclude, review, []
    header = data[:HEADER_BYTES]
    marker = GENERATED_MARKER.search(header)
    if marker:
        review.append(f"header marker {marker.group(0).decode(errors='replace')!r} suggests generated or vendor code")
    if COPYRIGHT_MARKER.search(header):
        review.append("header has a copyright or licence line; check it is mPainter's")
    try:
        data.decode("utf-8")
    except UnicodeDecodeError:
        review.append("content is not UTF-8")
    if b"\r" in data:
        review.append("content has carriage returns")
    risks = sorted(name for name, pattern in RISK_PATTERNS.items() if pattern.search(data))
    return exclude, review, risks


def classify(root, mode, kind, oid, raw_path):
    entry = {"path": raw_path.decode("utf-8", "backslashreplace"), "mode": mode, "blob": oid}
    path_issues = path_problems(raw_path)
    if path_issues:
        return dict(entry, status="review", reasons=path_issues)
    path = raw_path.decode("utf-8")
    parts = path.split("/")
    if kind == "commit" or mode == "160000":
        return dict(entry, status="exclude", reasons=["gitlink (submodule)"])
    if mode == "120000":
        return dict(entry, status="exclude", reasons=["symlink in git"])
    foreign = sorted({FOREIGN_COMPONENTS[p] for p in parts[:-1] if p in FOREIGN_COMPONENTS})
    if foreign:
        return dict(entry, status="exclude", reasons=[f"{name} path" for name in foreign])
    if (path.startswith(PLATFORM_PREFIXES) or path.endswith(PLATFORM_EXTENSIONS)
            or any(p.lower() in PLATFORM_COMPONENTS for p in parts[:-1])):
        return dict(entry, status="exclude", reasons=["platform-specific path"])
    group = ownership(path)
    if group is None:
        return dict(entry, status="review", reasons=["ownership unknown: no explicit rule"])
    entry["owner"] = group
    if mode != "100644":
        return dict(entry, status="review", reasons=[f"git mode {mode}, expected 100644"])
    issues, data = disk_problems(root, path)
    if issues:
        return dict(entry, status="exclude", reasons=issues)
    licence = licence_folder(root, path)
    if licence:
        return dict(entry, status="review", reasons=[f"folder has its own licence file {licence}"])
    if blob_id(data, len(oid)) != oid:
        return dict(entry, status="review", reasons=["working-tree bytes differ from the base blob"])
    entry["sha256"] = hashlib.sha256(data).hexdigest()
    entry["bytes"] = len(data)
    exclude, review, risks = content_findings(data)
    entry["risks"] = risks
    if exclude:
        return dict(entry, status="exclude", reasons=exclude)
    if review:
        return dict(entry, status="review", reasons=review)
    return dict(entry, status="include", reasons=[f"owned: {group}"])


def load_write_scope(root, scope_path):
    if scope_path is None:
        return None
    data = json.loads(safe_input(root, scope_path).read_text(encoding="utf-8"))
    paths = data.get("tracked_write_candidates")
    if not isinstance(paths, list) or not all(isinstance(p, str) for p in paths):
        raise ManifestError("write scope has no list of tracked_write_candidates")
    return set(paths)


def safe_input(root, rel):
    path = (root / rel)
    if path.is_symlink() or root not in path.resolve().parents:
        raise ManifestError(f"{rel} is a symlink or outside the root")
    return path


def build(root, base, scope=None):
    folder_names.cache_clear()
    root = Path(root).resolve()
    top = Path(git(root, "rev-parse", "--show-toplevel").decode().rstrip("\n")).resolve()
    if top != root:
        raise ManifestError(f"--root must be the top of the worktree ({top})")
    commit = git(root, "rev-parse", "--verify", "--end-of-options", f"{base}^{{commit}}").decode().strip()
    files = []
    for mode, kind, oid, raw_path in tree_entries(root, commit):
        if raw_path.lower().endswith(tuple(e.encode() for e in FRAGMENT_EXTENSIONS)):
            files.append({"path": raw_path.decode("utf-8", "backslashreplace"), "mode": mode, "blob": oid,
                          "status": "review", "reasons": ["C++ fragment for #include; not provable alone"]})
            continue
        if not raw_path.lower().endswith(tuple(e.encode() for e in EXTENSIONS)):
            continue
        entry = classify(root, mode, kind, oid, raw_path)
        if entry["status"] == "include" and not raw_path.endswith(tuple(e.encode() for e in EXTENSIONS)):
            entry = dict(entry, status="review", reasons=["extension is not lower case"])
        if scope is not None and entry["status"] == "include":
            entry["in_write_scope"] = entry["path"] in scope
        files.append(entry)
    files.sort(key=lambda entry: entry["path"])
    counts = {status: 0 for status in ("include", "exclude", "review")}
    reasons = {}
    for entry in files:
        counts[entry["status"]] += 1
        for reason in entry["reasons"]:
            key = f"{entry['status']}: {reason}"
            reasons[key] = reasons.get(key, 0) + 1
    summary = {"base": commit, "extensions": list(EXTENSIONS), "counts": counts,
               "reasons": dict(sorted(reasons.items()))}
    if scope is not None:
        summary["include_in_write_scope"] = sum(1 for e in files if e.get("in_write_scope"))
    return {"schema": 1, "summary": summary, "files": files}


def write_output(root, output, manifest):
    root = Path(root).resolve()
    out = Path(output)
    out = out if out.is_absolute() else root / out
    parent = out.parent
    if parent.is_symlink() or not parent.is_dir():
        raise ManifestError(f"output folder {parent} is missing or a symlink")
    if root not in parent.resolve().parents and parent.resolve() != root:
        raise ManifestError("output must be inside the root")
    data = json.dumps(manifest, indent=1, sort_keys=False, ensure_ascii=True) + "\n"
    try:
        with open(out, "x", encoding="utf-8") as handle:
            handle.write(data)
    except FileExistsError:
        raise ManifestError(f"{output} exists; it is kept, not overwritten")
    return out


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--root", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--base", default="HEAD")
    parser.add_argument("--write-scope")
    args = parser.parse_args(argv)
    try:
        root = Path(args.root).resolve()
        manifest = build(root, args.base, load_write_scope(root, args.write_scope))
        write_output(root, args.output, manifest)
    except (ManifestError, OSError, ValueError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 2
    summary = manifest["summary"]
    print(json.dumps(summary["counts"]), summary["base"])
    for key, count in summary["reasons"].items():
        print(f"{count:6} {key}")
    if "include_in_write_scope" in summary:
        print(f"included and in the write scope: {summary['include_in_write_scope']}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
