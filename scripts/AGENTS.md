# scripts

Build, package, test and gate scripts. `mp.py` is the entry point for the repository's own checks; the build and package scripts are still run directly.

## Entry points

| Script | What it does |
|---|---|
| `mp.py` | `check` (the repository gates), `move` (domain moves), `docs` (generated docs), `spdx`, `doctor`, `brief`, `version` |
| `prepare-blender.py` | Clones Blender v5.2.0, checks the pinned commit, applies the patches in `third_party/blender/patches/` in order |
| `build.py` | Configures and builds `bts-host` and Blender with CMake and Ninja, then installs. Refuses macOS today. |
| `check-environment.py` | Checks that the pinned Studio HDR is in the runtime |
| `check-release.py` | Focused branding, theme and asset checks, run by `bts-release-check` |
| `package-linux.py`, `package-appimage.py` | Linux tarball and AppImage |
| `package-macos.sh` | macOS DMG |
| `package-windows.ps1` | Native Windows zip. The cross-build uses `cross-windows/package.py` instead. |
| `sanitize-runtime.py` | Cleans build metadata and colour-space display names in the runtime |

## Folders

| Folder | What it holds |
|---|---|
| `host/` | About 300 build, run, verify and gate scripts. `verify-cpu.py` runs the CPU suite; `main_source.py` gives verifiers main()'s code as one text. |
| `verify/` | Generators and checks used by `mp.py`: `blender_surface.py`, `bts_variables.py`, `check_agent_docs.py`, `format_owned_manifest.py`, and the ratchets in `blender-ratchets.json` |
| `tests/` | Tests for `mp.py` and the shared script helpers |
| `cross-windows/` | The Windows cross-build. It has its own `AGENTS.md`. |
| `polyhaven/` | Download, generate and check Poly Haven materials |
| `blender/` | Blender-side preset scripts |
| `site/` | The bug-report endpoint |

## Rules for scripts

- Pass paths to CMake in forward-slash form (`pathlib.Path.as_posix()`). A backslash path broke the Windows CI install.
- Read the version from `pyproject.toml` and pins from `native/build-pins.json`. Never write them as literals.
- Search `native/host` and `native/harness` recursively. `tests/test_recursive_source_listing.py` fails on a flat listing of an owned folder.
- A verifier that reads source text must not depend on layout; match needles without whitespace.
- A script that changes state must reach the same result when it runs again after a crash.
