#!/usr/bin/env python3
"""Measure raw float Pointiness bakes against the exact exported Cycles surface."""
import json
import pathlib
import sys
import numpy as np

root = pathlib.Path(sys.argv[1])
results = []
for name, tolerance in [('displaced', 0.0001), ('shaderball', 0.005)]:
    reference = np.fromfile(root / 'fixtures' / f'blender-{name}-4.rgba', np.float32).reshape(256, 256, 4)
    errors = {}
    for variant in ['before', 'after']:
        pixels = np.fromfile(root / variant / f'{name}-4.rgba', np.float32).reshape(256, 256, 4)
        mask = (pixels[:, :, 3] > 0.99) & (reference[:, :, 3] > 0.99)
        assert np.isfinite(pixels).all() and mask.any(), (name, variant)
        delta = pixels[:, :, 0][mask] - reference[:, :, 0][mask]
        errors[variant] = float(np.abs(delta).mean())
        results.append(dict(mesh=name, variant=variant, covered=int(mask.sum()),
                            mae=errors[variant], rmse=float(np.sqrt((delta * delta).mean())),
                            maximum_error=float(np.abs(delta).max())))
    assert errors['after'] < tolerance, (name, errors)
    assert errors['after'] < errors['before'] / 10, (name, errors)
(root / 'pointiness-parity.json').write_text(json.dumps(results, indent=2) + '\n')
print(json.dumps(results, indent=2))
