#!/usr/bin/env python3
"""Require the licensed, pinned Studio environment in a Linux runtime/AppDir."""
import hashlib
import pathlib
import sys

def check(root):
    world = pathlib.Path(root) / '5.2/datafiles/studiolights/world'
    image = world / 'default-studio.hdr'
    license_file = world / 'default-studio.txt'
    expected = '48505ffc80c0ca026775c77621b5a85a4c44b0fb7819ddadfa9e7bc6f5732031'
    if not image.is_file() or hashlib.sha256(image.read_bytes()).hexdigest() != expected:
        raise SystemExit('Missing or unexpected Studio HDRI: ' + str(image))
    if not license_file.is_file() or not all(term in license_file.read_text() for term in ('Mattias Lind', 'Blender', 'default-studio.hdr')):
        raise SystemExit('Missing Studio HDRI provenance: ' + str(license_file))
    print('Mattias Studio HDRI and provenance verified: ' + expected)

if __name__ == '__main__':
    check(sys.argv[1])
