#!/usr/bin/env python3
"""Fetch the pinned upstream release and apply the mPainter host integration."""
import argparse, pathlib, subprocess
PIN = 'fbe6228777e7d9afefcd61a413844e790ae75db7'
p = argparse.ArgumentParser()
p.add_argument('destination', type=pathlib.Path)
a = p.parse_args()
dest = a.destination.absolute()
patch = pathlib.Path(__file__).resolve().parents[1] / 'third_party/blender/patches/0001-native-host.patch'
def run(*args):
    return subprocess.check_output(['git', '-C', str(dest), *args], text=True).strip()
if not dest.exists():
    subprocess.run(['git', 'clone', '--depth', '1', '--branch', 'v5.2.0',
                    'https://projects.blender.org/blender/blender.git', str(dest)], check=True)
if run('rev-parse', 'HEAD') != PIN:
    raise SystemExit('Blender revision differs from the pinned v5.2.0 release; use a new destination.')
receipt = dest / '.git/mpainter-patch.sha256'
import hashlib
fingerprint = hashlib.sha256(patch.read_bytes()).hexdigest()
if receipt.exists():
    if receipt.read_text().strip() != fingerprint:
        raise SystemExit('Patch changed; prepare a fresh Blender source directory.')
    subprocess.run(['git', '-C', str(dest), 'apply', '--reverse', '--check', str(patch)], check=True)
else:
    if run('status', '--porcelain', '--untracked-files=normal'):
        raise SystemExit('Refusing to patch a modified source tree.')
    run('apply', '--check', str(patch))
    run('apply', str(patch))
    receipt.write_text(fingerprint + '\n')
print('Blender v5.2.0 native host integration is ready.')
