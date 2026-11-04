"""Compile/render the read-only mfPainter source, then compare all ten masks.

Run in bts-build under xvfb-run and run-parity-headless.sh. All generated
sources, resource objects and renders go into --output, never --reference.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import xml.sax.saxutils
from parity_test_environment import private_display

p = argparse.ArgumentParser()
p.add_argument('--reference', type=Path, required=True)
p.add_argument('--build', type=Path, required=True)
p.add_argument('--qt', type=Path, required=True)
p.add_argument('--output', type=Path, required=True)
a = p.parse_args()
private_display()
root = Path(__file__).resolve().parents[2]
ref, build, qt, out = [p.resolve() for p in (a.reference, a.build, a.qt, a.output)]
assert not out.is_relative_to(ref)
out.mkdir(parents=True, exist_ok=True)
inputs = [ref/'native/host/BuiltinSmartMasks.cc', ref/'native/host/AssetCatalog.cc',
          ref/'native/host/MaterialSpherePreview.hh', *sorted((ref/'assets/grunges').glob('*')),
          *sorted((ref/'assets/procedurals').glob('shaderball-jl01*.png'))]
def hashes():
    return {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs if p.is_file()}
before = hashes()
subprocess.run([str(qt/'libexec/rcc'), '--name', 'builtin_grunges',
                str(ref/'assets/grunges/builtin-grunges.qrc'), '-o', str(out/'grunges.cc')], check=True)
esc = xml.sax.saxutils.escape
(out/'preview.qrc').write_text('<RCC><qresource prefix="/builtin-procedurals">' + ''.join(
    f'<file alias="{p.name}">{esc(str(p))}</file>'
    for p in sorted((ref/'assets/procedurals').glob('shaderball-jl01*.png'))) + '</qresource></RCC>')
subprocess.run([str(qt/'libexec/rcc'), '--name', 'builtin_procedurals',
                str(out/'preview.qrc'), '-o', str(out/'preview.cc')], check=True)
command = ['c++', '-std=c++20', '-O1', '-fPIC', f'-I{ref}/native/host',
           f'-I{qt}/include', f'-I{qt}/include/QtCore', f'-I{qt}/include/QtGui',
           str(root/'native/tests/masknames_reference_export.cc'),
           str(ref/'native/host/BuiltinSmartMasks.cc'), str(out/'grunges.cc'), str(out/'preview.cc'),
           f'-L{qt}/lib', f'-Wl,-rpath,{qt}/lib', f'-L{build}/lib', '-lbts-export-config',
           '-lQt6Gui', '-lQt6Core', str(build.parent/'blender/lib/linux_x64/zlib/lib/libz.a'),
           '-o', str(out/'export-reference')]
subprocess.run(command, check=True)
env = dict(os.environ, QT_PLUGIN_PATH=str(qt/'plugins'), LD_LIBRARY_PATH=str(qt/'lib'))
subprocess.run([str(out/'export-reference'), str(out/'render')], env=env, check=True)
subprocess.run([str(build/'bin/bts-smart-mask-model-tests'), str(out/'actual'), str(out/'render')], env=env, check=True)
assert hashes() == before, 'Read-only reference changed'
(out/'source-hashes.json').write_text(json.dumps(before, indent=2) + '\n')
print('PASS: ten reference recipes and CPU thumbnails are pixel exact; reference unchanged')
