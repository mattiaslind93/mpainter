#!/usr/bin/env python3
"""Check the workspace's retained maps after rebakes, history and reopening."""
import json
import pathlib
import sys

root = pathlib.Path(sys.argv[1])
captures = json.loads((root / 'bake-fix.json').read_text())['captures']
bakes = [c for c in captures if c['stage'] == 'baked']
assert [c['bake_configuration']['resolution'] for c in bakes] == [64, 256, 1024, 256, 64, 1024, 64]
previous = {}
for capture in bakes:
    size = capture['bake_configuration']['resolution']
    maps = capture['mesh_maps']
    assert [m['role'] for m in maps] == list(range(7)), size
    assert all(m['hashes'] for m in maps), size
    if size in previous:
        assert maps == previous[size], ('different output at repeated resolution', size)
    previous[size] = maps
for stage, size in [('undo', 1024), ('redo', 64), ('maps-opened', 64)]:
    capture = next(c for c in captures if c['stage'] == stage)
    assert capture['mesh_maps'] == previous[size], stage
print('Workspace rebakes: seven maps at all sizes; repeat, undo/redo and reopen hashes exact: PASS')
