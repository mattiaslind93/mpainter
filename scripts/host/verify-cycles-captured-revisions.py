#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Repeat only the unchanged six-second actor against one new full integration."""
import argparse
import json
import os
from pathlib import Path
import resource
import cycles_captured_verifier as verifier

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--snapshot', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--history-root', type=Path, required=True)
    parser.add_argument('--jobs', type=int, choices=(1,2), default=2)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument('--build-only', action='store_true')
    mode.add_argument('--run-only', action='store_true')
    parser.add_argument('--expected-build-sha256', help='Mandatory for the separately admitted run-only step')
    args = parser.parse_args()
    if args.run_only != bool(args.expected_build_sha256): parser.error('Run-only requires exactly one expected build SHA')
    repo = Path(__file__).resolve().parents[2]
    snapshot = args.snapshot.resolve(); history = args.history_root.resolve()
    out = verifier.output_path(args.output, snapshot, repo.parent/'build')
    resource.setrlimit(resource.RLIMIT_CORE, (0,0))
    if args.build_only:
        verifier.build(snapshot, repo, out, history, args.jobs)
        print(json.dumps({'build': str(out/'build.json'), 'sha256': verifier.sha(out/'build.json')}))
        return 0
    return 0 if verifier.run(snapshot, repo, out, history, args.expected_build_sha256) else 1

if __name__ == '__main__': raise SystemExit(main())
