#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Refresh the isolated application from a commit, preserving unchanged mtimes.

Refuse edits to previously snapshotted files; Blender and library adaptations
are separate and are never touched. Run inside the container after env.sh.
"""
import argparse
import hashlib
import os
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--ref', default=os.environ.get('MP_WIN_SOURCE_REF', 'HEAD'))
args = parser.parse_args()
repo = Path(__file__).absolute().parents[2]
work = Path(os.environ['MP_WIN_WORK']) / 'build/windows'
source = work / 'mpainter-source'
record = work / 'source-head.txt'


def git(*args):
    return subprocess.check_output(['git', '-C', str(repo), *args])


def entries(ref):
    result = {}
    for entry in git('ls-tree', '-rz', ref).split(b'\0'):
        if not entry:
            continue
        metadata, name = entry.split(b'\t', 1)
        mode, kind, oid = metadata.decode().split()
        if kind != 'blob':
            raise SystemExit('Unexpected submodule in application snapshot')
        result[os.fsdecode(name)] = (mode, oid)
    return result


def contents(path, mode):
    return os.fsencode(os.readlink(path)) if mode == '120000' else path.read_bytes()


ref = git('rev-parse', args.ref + '^{commit}').decode().strip()
previous = entries(record.read_text().strip()) if record.exists() else {}
desired = entries(ref)
# Validate everything before making any changes.
for name, (mode, oid) in previous.items():
    data = contents(source / name, mode)
    actual = hashlib.sha1(b'blob ' + str(len(data)).encode() + b'\0' + data).hexdigest()
    if actual != oid:
        raise SystemExit(f'Local snapshot edit; refusing to overwrite: {name}')
for name in desired.keys() - previous.keys():
    if (source / name).exists() or (source / name).is_symlink():
        raise SystemExit(f'Untracked snapshot file would be overwritten: {name}')
changed = 0
for name, (mode, oid) in desired.items():
    if previous.get(name) == (mode, oid):
        continue
    path = source / name
    path.parent.mkdir(parents=True, exist_ok=True)
    if path.is_symlink():
        path.unlink()
    data = git('cat-file', 'blob', oid)
    if mode == '120000':
        path.unlink(missing_ok=True)
        path.symlink_to(os.fsdecode(data))
    else:
        path.write_bytes(data)
        path.chmod(int(mode, 8) & 0o777)
    changed += 1
for name in previous.keys() - desired.keys():
    (source / name).unlink()
record.write_text(ref + '\n')
print(f'{ref}: {len(desired)} files, {changed} updated, '
      f'{len(previous.keys() - desired.keys())} removed')
