# SPDX-License-Identifier: GPL-3.0-or-later
"""Attest a completed full artist build in an existing independent snapshot.

Run after the actual build confirmation and full CPU suite succeed. The input
confirmation JSON has returncode, command (argv), log (path) and source_head.
The original background install log is optional evidence, never an invented exit.
"""
import argparse
from pathlib import Path
from cycles_integrated_snapshot import capture

p = argparse.ArgumentParser(description=__doc__)
p.add_argument("--snapshot", type=Path, required=True)
p.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[2])
p.add_argument("--blender", type=Path, required=True)
p.add_argument("--build-result", type=Path, required=True)
p.add_argument("--cpu-results", type=Path, required=True)
p.add_argument("--fork-receipt", type=Path, required=True)
p.add_argument("--original-build-log", type=Path)
a = p.parse_args()
print(capture(a.snapshot, a.repo, a.blender, a.build_result, a.cpu_results, a.fork_receipt, a.original_build_log))
