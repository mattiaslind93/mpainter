#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Cross-build and package a Windows candidate from one committed revision, in the
# documented order: snapshot, verify, build, package, smoke, license gate.
#
#   candidate.sh start REF   run detached; log and pid in $MP_WIN_WORK/logs/windows
#   candidate.sh run REF     run in the foreground
#   candidate.sh wait        block until a detached run exits, then print its log tail
#
# REF must be this checkout's HEAD, because package.py refuses a runtime built from
# any other commit. Set MP_WIN_JOBS=2 while other builds share the machine.
set -euo pipefail
source "$(dirname "$0")/env.sh"
repo=$(cd -- "$MP_WIN_SCRIPTS/../.." && pwd)
log=$MP_WIN_WORK/logs/windows/candidate.log
pidfile=$MP_WIN_WORK/logs/windows/candidate.pid

case "${1:-}" in
  run)
    ref=$(git -C "$repo" rev-parse --verify "${2:?Pass the commit to build}^{commit}")
    [[ "$(git -C "$repo" rev-parse HEAD)" == "$ref" ]] ||
      { echo "package.py packages HEAD; check out $ref first" >&2; exit 1; }
    trap 'echo "CANDIDATE_EXIT rc=$? $(date -Is)"' EXIT
    echo "CANDIDATE_START $ref jobs=$MP_WIN_JOBS $(date -Is)"
    # A workspace stripped to its runtime cannot be rebuilt here; prepare-source.sh
    # re-fetches the Blender tree and Windows libraries over the network.
    missing=()
    for input in blender lib-windows_x64 mpainter-source dependency-source; do
      [[ -d "$MP_WIN_WORK/build/windows/$input" ]] || missing+=("build/windows/$input")
    done
    ((${#missing[@]} == 0)) ||
      { echo "Missing build inputs: ${missing[*]}; run bootstrap.sh and prepare-source.sh first" >&2; exit 1; }
    version=$(sed -n 's/^version = "\(.*\)"$/\1/p' "$repo/pyproject.toml")
    materials=$(git -C "$repo" ls-tree -r --name-only "$ref" assets/materials | wc -l)
    python3 "$MP_WIN_SCRIPTS/snapshot-source.py" --ref "$ref"
    python3 "$MP_WIN_SCRIPTS/verify-source.py"
    bash "$MP_WIN_SCRIPTS/build.sh"
    export MP_WIN_DIST=${MP_WIN_DIST:-$MP_WIN_WORK/dist/candidates/$(date +%F)}
    python3 "$MP_WIN_SCRIPTS/package.py"
    stage=$MP_WIN_DIST/mPainter-$version-windows-x64
    bash "$MP_WIN_SCRIPTS/smoke.sh" "$stage"
    python3 "$repo/scripts/license-gate.py" "$stage" --expect-commit "$ref" \
      --expect-version "$version" --expect-materials "$materials"
    cat "$stage.zip.sha256"
    ;;
  start)
    setsid nohup bash "${BASH_SOURCE[0]}" run "${2:?Pass the commit to build}" > "$log" 2>&1 < /dev/null &
    echo $! > "$pidfile"
    echo "Windows candidate started detached, pid $!, log $log"
    ;;
  wait)
    pid=$(cat "$pidfile")
    while kill -0 "$pid" 2>/dev/null; do sleep 30; done
    tail -n 80 "$log"
    ;;
  *) echo "usage: $0 start REF|run REF|wait" >&2; exit 2 ;;
esac
