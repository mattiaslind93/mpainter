# SPDX-License-Identifier: GPL-3.0-or-later
"""Apply the reviewed, idempotent Qt 6.8.3 dialog persistence patch."""
import argparse
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--source', required=True, type=Path)
args = parser.parse_args()
patch = Path(__file__).resolve().parent / 'patches/qt-6.8.3-file-dialog-settings.patch'
command = ['patch', '--batch', '-p1', '-d', str(args.source.resolve()), '-i', str(patch)]
if subprocess.run([*command, '--forward', '--dry-run'], capture_output=True).returncode == 0:
    subprocess.run([*command, '--forward'], check=True)
elif subprocess.run([*command, '--reverse', '--dry-run'], capture_output=True).returncode == 0:
    print('Qt file-dialog settings patch already applied')
else:
    raise SystemExit('Qt file-dialog patch does not match this source; refusing to modify it')
