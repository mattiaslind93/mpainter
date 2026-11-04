#!/usr/bin/env python3
"""Acquire unmodified x64 release CRT DLLs for local testing; see README terms."""
import hashlib
import os
from pathlib import Path
from urllib.request import urlopen
from zipfile import ZipFile

root = Path(os.environ['MP_WIN_WORK'])
sha = '4aaf54db0bfc9435f7c3660e1a00237a4b556042bfeea64bde44c2e0194e6ee5'
url = ('https://download.visualstudio.microsoft.com/download/pr/'
       f'45d3b8dd-bced-4b37-9974-142f748d710c/{sha}/'
       'Microsoft.VC.14.44.17.14.CRT.Redist.X64.base.vsix')
archive = root / 'cache/windows/crt-redist-14.44.35211.vsix'
if not archive.exists():
    with urlopen(url, timeout=90) as response:
        archive.write_bytes(response.read())
if hashlib.sha256(archive.read_bytes()).hexdigest() != sha:
    raise SystemExit('CRT archive checksum mismatch')
output = root / 'toolchains/windows/redist'
output.mkdir(parents=True, exist_ok=True)
with ZipFile(archive) as z:
    for entry in z.infolist():
        if '/x64/Microsoft.VC143.CRT/' in entry.filename and entry.filename.endswith('.dll'):
            name = Path(entry.filename).name
            (output / name).write_bytes(z.read(entry))
            print(name)
if not (output / 'vcruntime140.dll').exists():
    raise SystemExit('Expected release CRT DLL missing from archive')
