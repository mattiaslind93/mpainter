#!/usr/bin/env python3
"""Check downloaded release library payloads against Git LFS SHA-256 OIDs."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
root = Path(os.environ['MP_WIN_WORK'])
libs = root / 'build/windows/lib-windows_x64'
files = json.loads(subprocess.check_output(['git', '-C', str(libs), 'lfs', 'ls-files', '--json']))['files']
checked = 0
size = 0
errors = []
for record in files:
    p = Path(record['name'])
    if any(s.lower() == 'debug' for s in p.parts) or p.suffix == '.pdb':
        continue
    with (libs / p).open('rb') as stream:
        sha = hashlib.file_digest(stream, 'sha256').hexdigest()
    if sha != record['oid']:
        errors.append(str(p))
    checked += 1
    size += record['size']
result = {'checked': checked, 'bytes': size, 'mismatches': errors}
(root / 'logs/windows/lfs-integrity.json').write_text(json.dumps(result, indent=2) + '\n')
print(json.dumps(result))
raise SystemExit(bool(errors))
