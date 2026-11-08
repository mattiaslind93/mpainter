# scripts

Build, package, test and gate scripts. Phase 2 of `docs/plans/2026-11-08-restructure.md` adds one entry point, mp.py, and sorts this folder by purpose. Until then, use the scripts below directly.

## Entry points

| Script | What it does |
|---|---|
| `prepare-blender.py` | Clones Blender v5.2.0, checks the pinned commit, applies the patches in `third_party/blender/patches/` in order |
| `build.py` | Configures and builds `bts-host` and Blender with CMake and Ninja, then installs. Refuses macOS today. |
| `check-environment.py` | Checks that the pinned Studio HDR is in the runtime |
| `check-release.py` | Focused branding, theme and asset checks, run by `bts-release-check` |
| `package-linux.py`, `package-appimage.py` | Linux tarball and AppImage |
| `package-macos.sh` | macOS DMG |
| `package-windows.ps1` | Native Windows zip. The cross-build uses `cross-windows/package.py` instead. |
| `sanitize-runtime.py` | Cleans build metadata and colour-space display names in the runtime |
| `check-agent-docs.py` | Checks that every path in an `AGENTS.md` exists and that every `CLAUDE.md` is only `@AGENTS.md` |

## Folders

| Folder | What it holds |
|---|---|
| `host/` | About 300 build, run, verify and gate scripts. `verify-cpu.py` runs the CPU suite. |
| `cross-windows/` | The Windows cross-build. It has its own `AGENTS.md`. |
| `polyhaven/` | Download, generate and check Poly Haven materials |
| `blender/` | Blender-side preset scripts |
| `site/` | The bug-report endpoint |

## Rules for scripts

- Pass paths to CMake in forward-slash form (`pathlib.Path.as_posix()`). A backslash path broke the Windows CI install.
- Read the version from `pyproject.toml` and pins from `native/build-pins.json`. Never write them as literals. The Qt version and the Blender commit are still repeated in a few scripts; remove a copy when you touch it.
- Search `native/host` recursively. Eight scripts in `host/` do not yet, as listed in `native/host/AGENTS.md`.
- A script that changes state must reach the same result when it runs again after a crash.
