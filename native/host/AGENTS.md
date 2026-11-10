# native/host

The mPainter application, C++20, in domain folders. `docs/architecture/source-layout.md` is the map and lists the folders. Put every new file in a domain folder. The top level holds only `CMakeLists.txt`, `SourceIncludeDirs.cmake`, this file, and seven files that sha-pinned live-render tests in `native/tests/` include by `../host/` path: `main.cc`, `BlenderHost.hh`, `ColorPicker.hh`, `GpuProjectReadback.hh`, `PropertyLatency.hh`, `TextureDocument.hh` and `ViewportDemoCapture.hh`. They move when the owner renews those pins.

## How it builds

- This folder is not its own CMake project. Blender patch 0001 includes `CMakeLists.txt` from Blender's own source/creator build. `scripts/build.py` and `scripts/cross-windows/configure.sh` pass `-DBTS_HOST_SOURCE_DIR`.
- `CMakeLists.txt` lists every source file by name. There is no globbing. A new source file must be added to the target that compiles it.
- Files include each other by bare name across all domain folders. `SourceIncludeDirs.cmake` holds the list of folders (`BTS_HOST_DOMAINS`); add a new folder there in the same change. The standalone test projects in `native/tests/*/` include the same file.
- Move files with `python3 scripts/mp.py move <folder> --list <file>`. It refuses files that a script, test or JSON file names, rewrites the CMake paths, and keeps every file byte-identical.
- Qt is found with `find_package(Qt6 6.8.3 EXACT ...)`. The namespace is `bts`, an old code name for mPainter.

| Target | What it is |
|---|---|
| `bts-host` | The application. The binary is `mpainter` on Linux and `mPainter` on macOS and Windows. |
| `bts-export-config`, `bts-builtin-smartmasks` | Static libraries linked into the host and tests |
| `bts-<name>-tests` | One unit test program each, from `native/tests/` |
| `bts-cpu-tests` | Builds every `bts-*-tests` target |
| `bts-cpu-check` | Runs the full CPU suite through `scripts/host/verify-cpu.py` |
| `bts-release-check` | Focused branding, theme and asset checks through `scripts/check-release.py` |
| `bts-gpu-only-paths` | Runs `scripts/host/verify-gpu-only-paths.py` on every host build |

## Startup

`main()` in `main.cc` is short: process setup, the command line (product options; the harness options come from `native/harness/HarnessOptions.hh`), the early harness dispatch, the splash and the Blender host. It then calls `run_workspace_session()` in `app/WorkspaceSession.inc`, which includes its sections in order (`app/WorkspaceState.inc` to `app/WorkspaceWindow.inc`, then the harness sections) and ends with `app.exec()` and the shutdown.

The session is one translation unit with `main.cc` on purpose. The reviewed live-render drivers in `native/tests/` compile `main.cc` with a renamed `main()` and a `start_gui_fix_demo()` macro that names the session's locals. Verifiers that read main()'s code as text use `scripts/host/main_source.py`.

## Version

`BTS_PRODUCT_VERSION` comes from `pyproject.toml` through CMake and is set only on `app/Branding.cc`. Read it with `bts::product_version()`. Never write a version string in C++.

## Blender boundary

- Only `blender/` may include Blender headers. In this folder only `main.cc` and `GpuProjectReadback.hh` still include them outside `blender/` (pinned test includes); the rest of the count is harness and test code. `docs/blender-surface.md` lists every Blender header in use; `python3 scripts/mp.py check` counts the files outside `blender/` and fails if the count grows.
- Do not add Blender includes to UI, panel or widget files.

## Platform code

- OS-specific `#if` blocks on `_WIN32`, `__APPLE__` or `__linux__` sit in a few files, mainly `ResourceAvailability.hh`, `DiskScratch.hh` and `MeshMapStorage.hh`. Put new OS-specific code there, not in feature code.

## Windows compile rules

These broke the Windows build before. `mp check` catches the first and the fourth.

- Reset a `QUuid` with `QUuid()`, never `{}`.
- Do not name anything `near`, `far`, `min` or `max` where Windows headers can see it.
- Include every header you use, for example `<span>`. MSVC does not include it for you.
- Do not use `shared_ptr::unique()`. The C++20 library removed it.

## Harness

Demo, proof, witness and test code lives in `native/harness/` and has its own `AGENTS.md`. Do not include it from production code. `main()` reads harness options only through `bts::harness::HarnessContext`; `mp check` enforces that.

## GPU-only rule

Interactive paths run on the GPU. CPU evaluation is only for export and diagnostics. `scripts/host/verify-gpu-only-paths.py` checks it on every build.

## Formatting

The repo's `.clang-format` is Blender's style (column 99, includes not sorted). Format a file you change. Text verifiers must not depend on layout: match needles without whitespace (`compact()` in `scripts/host/main_source.py`).
