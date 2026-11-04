#!/usr/bin/env python3
"""Require the licensed, pinned Studio environment in a Linux runtime/AppDir."""
import hashlib
import pathlib
import sys

def check(root):
    world = pathlib.Path(root) / '5.2/datafiles/studiolights/world'
    image = world / 'studio.exr'
    license_file = world / 'license.txt'
    expected = 'c36c7fd390b2cdf8693360cf3662cb1d710c95fa799d9f822f42997167839b56'
    if not image.is_file() or hashlib.sha256(image.read_bytes()).hexdigest() != expected:
        raise SystemExit('Missing or unexpected Studio HDRI: ' + str(image))
    if not license_file.is_file() or not all(term in license_file.read_text() for term in ('CC0', 'Greg Zaal', 'Poly Haven', 'Studio:')):
        raise SystemExit('Missing Studio HDRI license: ' + str(license_file))
    print('Studio HDRI and CC0 notice verified: ' + expected)

if __name__ == '__main__':
    check(sys.argv[1])
