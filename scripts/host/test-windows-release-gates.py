#!/usr/bin/env python3
"""Keep the Windows build and packaging gates verified at 0.9.2 in place.

See "Lessons from the verified 0.9.2 build" in scripts/cross-windows/README.md.
"""
import ast
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path

repo = Path(__file__).resolve().parents[2]
cross = repo / 'scripts/cross-windows'
errors = []


def check_pe(imports):
    """Run check-pe.py on a fake runtime whose PE files all report `imports`."""
    with tempfile.TemporaryDirectory() as tmp:
        tmp = Path(tmp)
        runtime = tmp / 'runtime'
        runtime.mkdir()
        for name in ('mPainter.exe', 'tbbmalloc_proxy.dll'):
            (runtime / name).write_bytes(b'MZ')
        tool = tmp / 'bin/llvm-readobj'
        tool.parent.mkdir()
        tool.write_text('#!/bin/sh\necho IMAGE_FILE_MACHINE_AMD64\n'
                        + ''.join(f"echo '  Name: {name}'\n" for name in imports))
        tool.chmod(0o755)
        env = dict(os.environ, PATH=f'{tool.parent}{os.pathsep}{os.environ["PATH"]}')
        return subprocess.run([sys.executable, str(cross / 'check-pe.py'), str(runtime),
                               '--output', str(tmp / 'imports.json')],
                              env=env, capture_output=True, text=True)


# Shipping tbbmalloc_proxy.dll is allowed; importing it from mPainter.exe crashes startup.
result = check_pe(['KERNEL32.dll'])
if result.returncode:
    errors.append(f'check-pe.py rejected a clean runtime: {result.stdout}{result.stderr}')
result = check_pe(['KERNEL32.dll', 'tbbmalloc_proxy.dll'])
if result.returncode == 0 or 'tbbmalloc_proxy' not in result.stderr:
    errors.append('check-pe.py accepted mPainter.exe importing tbbmalloc_proxy.dll')

package = cross / 'package.py'
software_gl = next((ast.literal_eval(node.value) for node in ast.walk(ast.parse(package.read_text()))
                    if isinstance(node, ast.Assign)
                    and any(isinstance(t, ast.Name) and t.id == 'software_gl' for t in node.targets)), set())
for dll in ('opengl32.dll', 'libgallium_wgl.dll'):
    if dll not in software_gl:
        errors.append(f'package.py no longer refuses the Mesa test DLL {dll}')

for path in sorted(cross.rglob('*')):
    if path.suffix in {'.py', '.sh', '.md'} and re.search(r'mPainter-\d', path.read_text()):
        errors.append(f'{path.relative_to(repo)} hardcodes a package version; derive it from pyproject.toml')

build = (cross / 'build.sh').read_text()
if not 0 <= build.find('check-windows-uuid-resets.py') < build.find('configure.sh'):
    errors.append('build.sh must run check-windows-uuid-resets.py before configuring')
if 'check-windows-uuid-resets.py' not in (repo / '.github/workflows/release.yml').read_text():
    errors.append('release.yml no longer runs check-windows-uuid-resets.py')
verify = (repo / 'scripts/host/verify-cpu.py').read_text()
for script in ('check-windows-uuid-resets.py', 'test-windows-uuid-resets.py', Path(__file__).name):
    if f"'{script}'" not in verify:
        errors.append(f'verify-cpu.py no longer runs {script}')

if errors:
    raise SystemExit('\n'.join(errors))
print('Windows release gates are wired')
