#!/usr/bin/env python3
"""Repeatable native authored-Height acceptance; coordinate CPU/GPU slots first.

Run inside the existing compiler container. A full matching integrated snapshot
is mandatory. No production compilation, source conversion or fork patch replay.
"""
import argparse
import os
from pathlib import Path
from native_height_verifier import build, require, run_profiles


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--integrated-snapshot", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--environment", type=Path, required=True)
    parser.add_argument("--confetti-library", type=Path)
    parser.add_argument("--fixtures-manifest", type=Path)
    parser.add_argument("--profile", choices=("native", "artist", "lifecycle", "all", "rendering"), default="all",
                        help="Rendering is a separate native Roughness/raw-H acceptance; all keeps its existing scope")
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--build-only", action="store_true")
    mode.add_argument("--run-only", action="store_true")
    parser.add_argument("--jobs", type=int, choices=(1, 2), default=1,
                        help="Reserved slot budget; the two compilations run sequentially")
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[2]
    snapshot, output = args.integrated_snapshot.resolve(), args.output.resolve()
    bts = Path(os.environ.get("BTS_ROOT", repo.parent)).resolve()
    require(output.is_relative_to(bts / "build") and output != bts / "build" and
            not output.is_relative_to(bts / "build/host") and not output.is_relative_to(snapshot) and
            not snapshot.is_relative_to(output), "Use a private build output outside artist/snapshot storage")
    if hasattr(os, "nice") and os.nice(0) < 19:
        os.nice(19 - os.nice(0))
    output.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ, BTS_ROOT=str(bts), CCACHE_DIR=str(bts / "build/ccache"))
    if not args.run_only:
        build(snapshot, repo, output, env)
    if not args.build_only:
        require(args.profile in ("native", "all", "rendering") or args.fixtures_manifest is not None,
                "Standalone artist/lifecycle needs --fixtures-manifest from this exact test build")
        attempt = run_profiles(snapshot, repo, output, env, args.profile, args.environment.resolve(),
                               args.confetti_library.resolve() if args.confetti_library else None,
                               args.fixtures_manifest.resolve() if args.fixtures_manifest else None)
        print("Native Height policy " + args.profile + " PASS: " + str(attempt), flush=True)


if __name__ == "__main__":
    main()
