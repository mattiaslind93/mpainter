#!/usr/bin/env python3
"""Verify every snapshotted application file against the recorded Git commit."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
root = Path(os.environ['MP_WIN_WORK'])
repo = Path(__file__).absolute().parents[2]
source = root / 'build/windows/mpainter-source'
ref = (root / 'build/windows/source-head.txt').read_text().strip()
entries = subprocess.check_output(['git', '-C', str(repo), 'ls-tree', '-rz', ref]).split(b'\0')
errors = []
checked = 0
for entry in entries:
    if not entry:
        continue
    metadata, name = entry.split(b'\t', 1)
    mode, kind, oid = metadata.split()
    if kind != b'blob':
        raise SystemExit('Unexpected submodule in application snapshot')
    p = source / os.fsdecode(name)
    data = os.fsencode(os.readlink(p)) if mode == b'120000' else p.read_bytes()
    actual = hashlib.sha1(b'blob ' + str(len(data)).encode() + b'\0' + data).hexdigest()
    if actual != oid.decode():
        errors.append(os.fsdecode(name))
    checked += 1
result = {'commit': ref, 'checked_files': checked, 'mismatches': errors}
(root / 'logs/windows/source-integrity.json').write_text(json.dumps(result, indent=2) + '\n')
print(json.dumps(result))
raise SystemExit(bool(errors))
