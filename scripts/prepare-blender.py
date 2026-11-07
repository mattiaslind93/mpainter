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
def applies(*args):
    return subprocess.run(['git', '-C', str(dest), 'apply', *args, '--check'],
                          capture_output=True).returncode == 0
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
# Small follow-up patches have independent receipts so an existing integration
# can be extended without discarding its build cache.
for extra in sorted(patch.parent.glob('[0-9]*.patch')):
    if extra == patch:
        continue
    extra_receipt = dest / '.git' / ('mpainter-' + extra.name + '.sha256')
    extra_fingerprint = hashlib.sha256(extra.read_bytes()).hexdigest()
    if extra_receipt.exists():
        if extra_receipt.read_text().strip() != extra_fingerprint:
            raise SystemExit('Follow-up patch changed; prepare a fresh Blender source directory.')
        run('apply', '--reverse', '--check', str(extra))
    elif applies('--reverse', str(extra)):
        # A build workspace may already carry a follow-up change through its own
        # platform adaptation, such as scripts/cross-windows/prepare-cross.py.
        # The identical change is a receipt rather than a conflict; only a tree
        # that neither accepts nor already contains the patch is an error.
        extra_receipt.write_text(extra_fingerprint + '\n')
    else:
        run('apply', '--check', str(extra))
        run('apply', str(extra))
        extra_receipt.write_text(extra_fingerprint + '\n')
print('Blender v5.2.0 native host integration is ready.')
