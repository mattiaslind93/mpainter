# mPainter

mPainter is a GPL-3.0 3D texture painting application by Mattias Lind. It runs a Qt 6.8.3 workspace and Blender 5.2 in one process. The host app in `native/host/` is built inside Blender's own CMake build through Blender patch 0001. Customers buy builds on Gumroad, so every rule below protects a paid release.

This file is the context for every agent and new contributor. The restructure plan this branch follows (phases 1 to 7) lives on the docs/restructure-plan branch; `docs/architecture/source-layout.md` describes where the code is today.

## Hard rules

1. The version lives only in `pyproject.toml`. CMake reads it into `BTS_PRODUCT_VERSION`.
2. Every platform builds from the same commit on `main`. No long-lived platform branches.
3. A feature is done when it builds and passes the tests on Linux, Windows and macOS.
4. Blender sources change only through numbered patches in `third_party/blender/patches/`. A patch applies to every platform.
5. No fix stays local. Push it to a branch and open a PR the same day.
6. Production code never includes test or demo harness code. Harness code lives in `native/harness/`.
7. Only `native/host/blender/` may include Blender headers. Files elsewhere that still do are being moved there; `mp check` counts them and the count may only go down (`scripts/verify/blender-ratchets.json`).
8. Nothing reaches `main` without green checks on every platform.
9. Nothing reaches Gumroad without Mattias's approval.

## Map

| Path | What it holds | Own AGENTS.md |
|---|---|---|
| `native/host/` | The application, in domain folders (`app/`, `blender/`, `painting/`, `layers/`, `gpu/`, `ui/`, ...) | yes |
| `native/harness/` | Test, demo and proof code that `main()` still calls; leaves the release build later | yes |
| `native/tests/` | Unit tests, plain `assert` programs named `*_test.cc` | yes |
| `native/spsm/` | Standalone `.spsm` reader (HDF5 and LZ4) | no |
| `native/sbsar_metadata/` | Standalone SBSAR metadata reader, not part of the host build | no |
| `native/build-pins.json` | Blender commit, Qt version and toolchain pins | no |
| `scripts/` | Build, package, test and gate scripts, and `mp.py` | yes |
| `scripts/cross-windows/` | Windows x64 cross-build from Linux | yes |
| `third_party/` | Blender patches and OptiX headers | no |
| `packaging/` | Platform metadata templates (desktop file, metainfo, rc, Info.plist) | no |
| `assets/` | Shipped assets: branding, theme, meshes, environments, masks | no |
| `docs/` | Human documentation: `docs/contributing.md`, `docs/architecture/`, `docs/building.md` | no |

## Common commands

| Task | Command |
|---|---|
| Fetch and patch Blender | `python3 scripts/prepare-blender.py ../build/blender` |
| Configure and build (Linux) | `python3 scripts/build.py --blender ../build/blender --build ../build/linux --qt <qt-root>` |
| Full CPU test suite | `cmake --build <build> --target bts-cpu-check` |
| Focused release checks | `cmake --build <build> --target bts-release-check` |
| Repository gates | `python3 scripts/mp.py check` |
| Move host files into a domain | `python3 scripts/mp.py move <folder> --list <file>` |
| Regenerate the generated docs | `python3 scripts/mp.py docs` |
| Check the agent docs | `python3 scripts/verify/check_agent_docs.py --root .` |

`scripts/build.py` refuses macOS until the Metal port is merged.

## Platforms

- Linux x86_64 builds on Bazzite in the `bts-build` distrobox.
- Windows x64 builds on Bazzite with the clang-cl cross-build in `scripts/cross-windows/`. It is tested in a Windows 11 VM with emulated graphics. There is no real Windows machine.
- macOS arm64 builds and signs on the Mac Studio. The Metal port is not on `main` yet.

## How work is done

- Coder manages the work and starts Claude and Codex workers. A worker gets one task, on one platform, in one worktree, on one branch.
- Commits, branches, pull requests and comments follow `docs/contributing.md`.

## Known stale docs

`docs/building.md` still names an old version and says `bts-cpu-check` runs the focused release tests. Trust `pyproject.toml` and `native/host/CMakeLists.txt` over it until it is split per platform.
