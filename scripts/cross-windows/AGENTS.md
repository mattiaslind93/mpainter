# scripts/cross-windows

Builds the Windows x64 package on Linux with clang-cl and lld, against the pinned Blender 5.2 Windows libraries and Qt 6.8.3 `win64_msvc2022_64`. Run it inside the `bts-build` distrobox on Bazzite. `README.md` in this folder has the full steps and history.

## Order

1. `source env.sh`. `MP_WIN_JOBS` sets build parallelism, from 1 to 6. Use 2 when other heavy work runs.
2. `bootstrap.sh`, `prepare-source.sh`, `probe.sh`, `qt-probe.sh`, `build.sh`, in that order.
3. `package.py` makes the zip. `smoke.sh` and `capture.sh` run it under Wine.

`build.sh` runs the UUID reset check, `prepare-cross.py` and `fix-library-case.py` before it builds. `build.sh resume` skips those steps and configure, and continues an interrupted build.

Wine generator steps run inside one warm session started by `wine-session.sh`. `init-wine.sh` finishes any new or updated prefix setup first, with a time limit. If that setup runs inside the build's own session, it never finishes, and every Wine step then waits out Wine's five-minute boot timeout.

## Gates already in place

- `check-pe.py` checks that every Windows binary's imports resolve.
- `package.py` refuses Mesa and llvmpipe DLLs, file names that collide when case is ignored, and a version that differs from `pyproject.toml`.

## What a pass means

- Wine is not Windows. A Wine pass is a smoke test only.
- The real check is the Windows 11 VM on Bazzite, which has emulated graphics and no GPU. Mesa DLLs may be copied next to the exe inside the VM only, never into the zip.
- Do not ship Microsoft SDK or debug runtime files. Only the redistributable runtime may go in the package.

## Known problems

- `prepare-source.sh` stops on a workspace that `build.sh` has already prepared, because `fix-library-case.py` leaves case aliases behind. Do not clean or reset the library checkout to get past it. Report it instead. Phase 2 of the plan makes these steps safe to run again.
- The zip has no Qt TLS plugin, so HTTPS fails in the Windows app.
