# mPainter

mPainter is a GPL-3.0 3D texture painting application by Mattias Lind. It runs a Qt 6.8.3 workspace and Blender 5.2 in one process. The host app in `native/host/` is built inside Blender's own CMake build through Blender patch 0001. Customers buy builds on Gumroad, so every rule below protects a paid release.

This file is the context for every agent and new contributor. `CLAUDE.md` files contain only `@AGENTS.md`, so Claude Code and Codex read the same text. The plan for the current restructuring is `docs/plans/2026-11-08-restructure.md`.

## Hard rules

1. The version lives only in `pyproject.toml`. CMake reads it into `BTS_PRODUCT_VERSION`.
2. Every platform builds from the same commit on `main`. No long-lived platform branches.
3. A feature is done when it builds and passes the tests on Linux, Windows and macOS.
4. Blender sources change only through numbered patches in `third_party/blender/patches/`. A patch applies to every platform.
5. No fix stays local. Push it to a branch and open a PR the same day.
6. Production code never includes test or demo harness code.
7. New Blender includes go only in the bridge files (`native/host/BlenderHost.cc`, `native/host/SceneBridge.cc`). See `native/host/AGENTS.md`.
8. Nothing reaches `main` without green checks on every platform.
9. Nothing reaches Gumroad without Mattias's approval.

## Map

| Path | What it holds | Own AGENTS.md |
|---|---|---|
| `native/host/` | The application: about 1 000 files in one flat folder today | yes |
| `native/tests/` | Unit tests, plain `assert` programs named `*_test.cc` | yes |
| `native/spsm/` | Standalone `.spsm` reader (HDF5 and LZ4) | no |
| `native/sbsar_metadata/` | Standalone SBSAR metadata reader, not part of the host build | no |
| `native/build-pins.json` | Blender commit, Qt version and toolchain pins | no |
| `scripts/` | Build, package, test and gate scripts | yes |
| `scripts/cross-windows/` | Windows x64 cross-build from Linux | yes |
| `third_party/` | Blender patches and OptiX headers | yes |
| `packaging/` | Platform metadata templates (desktop file, metainfo, rc, Info.plist) | no |
| `assets/` | Shipped assets: branding, theme, meshes, environments, masks | no |
| `docs/` | Human documentation, plans and agent guides | no |

## Common commands

| Task | Command |
|---|---|
| Fetch and patch Blender | `python3 scripts/prepare-blender.py ../build/blender` |
| Configure and build (Linux) | `python3 scripts/build.py --blender ../build/blender --build ../build/linux --qt <qt-root>` |
| Full CPU test suite | `cmake --build <build> --target bts-cpu-check` |
| Focused release checks | `cmake --build <build> --target bts-release-check` |
| Check the agent docs | `python3 scripts/check-agent-docs.py` |

`scripts/build.py` refuses macOS until the Metal port is merged.

## Platforms

- Linux x86_64 builds on Bazzite in the `bts-build` distrobox.
- Windows x64 builds on Bazzite with the clang-cl cross-build in `scripts/cross-windows/`. It is tested in a Windows 11 VM with emulated graphics. There is no real Windows machine.
- macOS arm64 builds and signs on the Mac Studio. The Metal port is not on `main` yet.

## How work is done

- Coder, a GrokBot agent, manages the work and starts Claude and Codex workers. Its rules are in `docs/agents/coder.md`.
- A worker gets one task, on one platform, in one worktree, on one branch. The brief and report formats are `docs/agents/brief.md` and `docs/agents/report.md`.
- Commits, branches, pull requests and comments follow `docs/contributing.md`.

## Known stale docs

`docs/building.md` still names version 0.9.0-alpha and says `bts-cpu-check` runs the focused release tests. Trust `pyproject.toml` and `native/host/CMakeLists.txt` over it until it is split per platform.
