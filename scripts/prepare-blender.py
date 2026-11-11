#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Fetch the pinned upstream release and apply the mPainter host integration."""
import argparse, pathlib, subprocess, shutil, tempfile
PIN = 'fbe6228777e7d9afefcd61a413844e790ae75db7'
p = argparse.ArgumentParser()
p.add_argument('destination', type=pathlib.Path)
a = p.parse_args()
dest = a.destination.absolute()
patch = pathlib.Path(__file__).resolve().parents[1] / 'third_party/blender/patches/0001-native-host.patch'
def run(*args):
    return subprocess.check_output(['git', '-C', str(dest), *args], text=True).strip()
def verify_reverse(target):
    command = ['git', '-C', str(dest), 'apply', '--reverse', '--check', str(target)]
    if subprocess.run(command, capture_output=True).returncode == 0:
        return
    # Follow-up patches may intentionally extend a prior hunk. Validate the
    # reverse chain on private copies instead of undoing the live source tree.
    paths = {line[6:] for line in target.read_text().splitlines() if line.startswith('+++ b/')}
    scratch_root = dest.parent / 'tmp'
    scratch_root.mkdir(exist_ok=True)
    scratch = pathlib.Path(tempfile.mkdtemp(prefix='patch-chain-', dir=scratch_root))
    for path in paths:
        source = dest / path
        if source.is_file():
            output = scratch / path
            output.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source, output)
    for later in reversed(sorted(patch.parent.glob('[0-9]*.patch'))):
        if later.name <= target.name:
            continue
        touched = {line[6:] for line in later.read_text().splitlines() if line.startswith('+++ b/')}
        if not paths.intersection(touched):
            continue
        saved = dest / '.git' / ('mpainter-' + later.name + '.sha256')
        digest = hashlib.sha256(later.read_bytes()).hexdigest()
        if saved.exists() and saved.read_text().strip() != digest:
            raise SystemExit('Follow-up patch changed: ' + later.name)
        reverse = ['git', '-C', str(scratch), 'apply', '--reverse',
                   *['--include=' + name for name in sorted(paths)]]
        # A newer patch can be present in the checkout but not applied yet.
        # Do not try undoing it while validating an older installed receipt.
        applicable = subprocess.run([*reverse, '--check', str(later)], capture_output=True).returncode == 0
        if not applicable and not saved.exists():
            continue
        subprocess.run([*reverse, str(later)], check=True)
    subprocess.run(['git', '-C', str(scratch), 'apply', '--reverse', '--check', str(target)], check=True)
    print('Validated stacked patch:', target.name, '(copies retained at', scratch, ')')

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
    verify_reverse(patch)
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
        verify_reverse(extra)
    else:
        already_applied = subprocess.run(
            ['git', '-C', str(dest), 'apply', '--reverse', '--check', str(extra)],
            capture_output=True).returncode == 0
        if not already_applied:
            run('apply', '--check', str(extra))
            run('apply', str(extra))
        extra_receipt.write_text(extra_fingerprint + '\n')
print('Blender v5.2.0 native host integration is ready.')
