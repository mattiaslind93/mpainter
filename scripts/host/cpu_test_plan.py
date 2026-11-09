# SPDX-License-Identifier: GPL-3.0-or-later
"""Read the explicitly curated CMake CPU target group, never scan executables."""
import json
from pathlib import Path
import re


def cpu_test_binaries(build_dir: Path, configuration=None):
    build_dir = Path(build_dir).resolve()
    manifests = sorted(build_dir.glob('bts-cpu-tests-*.json'))
    if configuration is not None:
        manifests = [p for p in manifests
                     if p.name == f'bts-cpu-tests-{configuration}.json']
    if len(manifests) != 1:
        raise ValueError('Configure this source tree first to generate one CPU test '
                         'manifest, or select --config when several configurations exist')
    data = json.loads(manifests[0].read_text())
    names = data.get('binaries')
    if data.get('version') != 1 or not isinstance(names, list) or not names:
        raise ValueError('Invalid CMake CPU test manifest')
    result = []
    for name in names:
        if not isinstance(name, str) or not re.fullmatch(r'bts-[A-Za-z0-9_-]+-tests(?:\.exe)?', name):
            raise ValueError(f'Invalid CPU test executable name: {name!r}')
        if name in result:
            raise ValueError(f'Duplicate CPU test executable: {name}')
        result.append(name)
    return [build_dir / 'bin' / name for name in result]
