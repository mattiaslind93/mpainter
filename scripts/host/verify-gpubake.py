#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Check phase-one proofs, benchmark coverage and saved Cycles Pointiness parity."""
import argparse
from array import array
import json
import math
from pathlib import Path
import sys

parser = argparse.ArgumentParser()
parser.add_argument('--root', type=Path, required=True)
args = parser.parse_args()
root = args.root
proofs = root / 'shots/gpubake'
output = root / 'mpainter/docs/gpubake'
output.mkdir(parents=True, exist_ok=True)
proof = json.loads((proofs / 'phase-one.json').read_text())
assert len(proof['filters']) == 12
assert all(case['max_error'] <= 2e-5 and case['gpu_maps'] == 2 for case in proof['filters'])
assert all(proof['widgets'].values()) and len(proof['gui_bakes']) == 8
(output / 'phase-one.json').write_text(json.dumps(proof, indent=2) + '\n')
records = []
for size in (2048, 4096):
    for role in (3, 6, 4):
        for path in ('cpu', 'gpu'):
            record = json.loads((proofs / f'character-{size}-{role}-{path}.json').read_text())
            assert record['size'] == size and record['role'] == role
            assert record['gpu'] == (path == 'gpu')
            assert record['gpu_maps'] == (1 if path == 'gpu' else 0)
            assert record['covered_pixels'] > 0 and record['job_seconds'] > 0
            records.append(record)
(output / 'benchmarks.json').write_text(json.dumps(records, indent=2) + '\n')

def pixels(path):
    result = array('f')
    result.frombytes(path.read_bytes())
    if sys.byteorder != 'little':
        result.byteswap()
    assert len(result) == 256 * 256 * 4 and all(map(math.isfinite, result)), path
    return result

results = []
for mesh in ('displaced', 'shaderball'):
    for role in (3, 4, 6):
        cpu = pixels(root / f'build/gpubake/surface-cpu/{mesh}-{role}.rgba')
        gpu = pixels(root / f'build/gpubake/surface-gpu/{mesh}-{role}.rgba')
        errors = [abs(a - b) for a, b in zip(cpu, gpu)]
        result = dict(mesh=mesh, role=role, cpu_gpu_max=max(errors), cpu_gpu_mae=sum(errors) / len(errors))
        assert result['cpu_gpu_max'] <= 2e-5, result
        if role == 4:
            reference = pixels(root / f'build/crashcurv/fixtures/blender-{mesh}-4.rgba')
            covered = [i for i in range(0, len(cpu), 4) if cpu[i + 3] > .99 and reference[i + 3] > .99]
            assert covered
            for name, actual in (('cpu', cpu), ('gpu', gpu)):
                delta = [abs(actual[i] - reference[i]) for i in covered]
                result[name + '_blender_mae'] = sum(delta) / len(delta)
                result[name + '_blender_max'] = max(delta)
            assert result['gpu_blender_mae'] < (1e-4 if mesh == 'displaced' else .005), result
            result['covered'] = len(covered)
        results.append(result)
(output / 'surface-parity.json').write_text(json.dumps(results, indent=2) + '\n')
print(json.dumps(results, indent=2))
print('GPU phase one: filter/UI proofs, 12 benchmarks and Blender Pointiness parity PASS')
