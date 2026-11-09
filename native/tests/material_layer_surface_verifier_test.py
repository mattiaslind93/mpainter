# SPDX-License-Identifier: GPL-3.0-or-later
"""Critical causal gate: later sampling cannot justify an earlier raw capture."""
import ast
import copy
import hashlib
import tempfile
from pathlib import Path
import re
import unittest

source = Path(__file__).resolve().parents[2] / 'scripts/host/verify-material-layer-surface.py'
tree = ast.parse(source.read_text())
names = {'require', 'uint', 'integer', 'sha', 'causal_sampling_prefix', 'verify_duplicate_readback'}
nodes = [node for node in tree.body if isinstance(node, ast.FunctionDef) and node.name in names]
namespace = {'re': re, 'Path': Path, 'hashlib': hashlib}
exec(compile(ast.Module(body=nodes, type_ignores=[]), str(source), 'exec'), namespace)
prefix = namespace['causal_sampling_prefix']
duplicate = namespace['verify_duplicate_readback']


class CausalSampling(unittest.TestCase):
    def test_unique_terminal_requires_preceding_sampling(self):
        first = dict(phase=2, graph_matches=True, sample_index='0',
                     viewport_sampling_finished=False, reset_pending=True)
        next_sample = dict(first, sample_index='1', reset_pending=False)
        terminal = dict(first, sample_index='12', viewport_sampling_finished=True,
                        reset_pending=False, terminal=True)
        rows = [first, next_sample, terminal]
        self.assertEqual(prefix(rows, terminal, 2, 12), rows[:2])
        # Same data after the captured terminal does not establish its cause.
        with self.assertRaises(RuntimeError):
            prefix([terminal, first, next_sample], terminal, 2, 12)
        # One ambiguous duplicate cannot be chosen as the convenient terminal.
        with self.assertRaises(RuntimeError):
            prefix(rows + [copy.deepcopy(terminal)], terminal, 2, 12)
        # Unavailable/nonmatching graph rows do not establish sampling either.
        with self.assertRaises(RuntimeError):
            prefix([dict(first, graph_matches=False), next_sample, terminal], terminal, 2, 12)

    def test_duplicate_readback_requires_unchanged_bytes_and_identity(self):
        with tempfile.TemporaryDirectory() as folder:
            out = Path(folder)
            original = b'\x01\x02\x03\xff'*4
            first, second = out/'phase-1.rgba8', out/'phase-1.duplicate.rgba8'
            first.write_bytes(original); second.write_bytes(original)
            digest = hashlib.sha256(original).hexdigest()
            frame = dict(phase=1, raw=first.name, duplicate_raw=second.name,
                         duplicate_readback_no_pump=True, first_sha256=digest, duplicate_sha256=digest,
                         readback_draw_before='12', readback_draw_after='12',
                         readback_frames_before=20, readback_frames_after=20,
                         terminal=dict(host_draw='12', frames_after=20))
            duplicate(frame, out)
            with self.assertRaises(RuntimeError):
                duplicate(dict(frame, readback_draw_after='13'), out)
            with self.assertRaises(RuntimeError):
                duplicate(dict(frame, readback_frames_after=21), out)
            second.write_bytes(b'\x00'+original[1:])
            with self.assertRaises(RuntimeError):
                duplicate(frame, out)  # Retained hash mismatch.
            with self.assertRaises(RuntimeError):
                duplicate(dict(frame, duplicate_sha256=hashlib.sha256(second.read_bytes()).hexdigest()), out)
                # Self-consistent individual hashes cannot hide changed bytes.


if __name__ == '__main__':
    unittest.main()
