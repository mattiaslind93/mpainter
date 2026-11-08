# native/host

The mPainter application. About 1 000 C++20 files sit in this one folder today, plus `openpbr/`. Phase 5 of `docs/plans/2026-11-08-restructure.md` moves them into domain folders. Until then, find code by name. Do not start the move by hand.

## How it builds

- This folder is not its own CMake project. Blender patch 0001 adds `include("${BTS_HOST_SOURCE_DIR}/CMakeLists.txt")` to Blender's own source/creator/CMakeLists.txt in the Blender tree. `scripts/build.py` and `scripts/cross-windows/configure.sh` pass `-DBTS_HOST_SOURCE_DIR`.
- `CMakeLists.txt` lists every source file by name. There is no globbing. A new source file must be added to the target that compiles it.
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

`main()` in `main.cc` runs in this order:

1. `--version` prints the version before any Qt setup.
2. Test-session setup and stdout buffering. Windows uses `_IONBF`, because `_IOLBF` with size 0 is invalid in the MSVC runtime.
3. Qt attributes and an OpenGL 4.3 Core `QSurfaceFormat`.
4. Numeric locale and portable fonts, then `bts::ProfileApplication` (a `QApplication`).
5. `configure_theme`, which calls `bts::theme::apply`.
6. Command-line options, then the `bts::BrandingStartup` splash.
7. `bts::BlenderHost`, then the `QMainWindow` and `bts::WorkspaceShell`, then `app.exec()`.

`main.cc` is about 8 100 lines. Phase 7 splits it.

## Version

`BTS_PRODUCT_VERSION` comes from `pyproject.toml` through CMake and is set only on `Branding.cc`. Read it with `bts::product_version()`. Never write a version string in C++.

## Blender boundary

- 158 files here include Blender headers directly. The most are in `BlenderHost.cc`, `MeshImport.cc`, `ProjectIO.cc`, `UvPreview.cc` and `NativeMaterialFragmentGpuCompiler.cc`.
- The bridge files are `BlenderHost.cc`, `SceneBridge.cc` and `BlenderPresetBridge.cc`. New Blender calls go through them. Do not add Blender includes to UI, panel or widget files.
- Phase 5 moves the bridge into its own folder and adds a check that stops Blender includes elsewhere.

## Platform code

- 14 files have `#if` blocks on `_WIN32`, `__APPLE__` or `__linux__`. The main ones are `ResourceAvailability.hh`, `ResourceStack.hh`, `DiskScratch.hh`, `HistoryPageStorage.hh`, `ProjectFileSafety.hh` and `MeshMapStorage.hh`.
- Put new OS-specific code in those files, not in feature code.

## Windows compile rules

These broke the Windows build before. Check for them in every change.

- Reset a `QUuid` with `QUuid()`, never `{}`. PR #1 adds a check script that enforces it.
- Do not name anything `near`, `far`, `min` or `max` where Windows headers can see it.
- Include every header you use, for example `<span>`. MSVC does not include it for you.
- Do not use `shared_ptr::unique()`. The C++20 library removed it.
- Saving a file on Windows needs the `QTemporaryFile` handle released before the replace. See `ProjectFileSafety.hh`.

## Test harness inside the app

Files with `Demo`, `Proof`, `Witness`, `Bench` or `Test` in the name are test harness code that is compiled into the app today. Do not include them from production code. Phase 4 moves them out.

## GPU-only rule

Interactive paths run on the GPU. CPU evaluation is only for export and diagnostics. `scripts/host/verify-gpu-only-paths.py` checks it on every build.

## Scripts that miss subfolders

These scripts read only the top of this folder and miss `openpbr/`. Keep that in mind when you add a subfolder, until phase 5 fixes them:

- `scripts/host/verify-gpu-only-paths.py`
- `scripts/host/check-theme-colors.py`
- `scripts/host/verify-numeric-locale.py`
- `scripts/host/verify-trackball.py`
- `scripts/host/verify-native-real-material-ui.py`
- `scripts/host/verify-native-vector-mapping-ui.py`
- `scripts/host/verify-native-image-ui.py`
- `scripts/host/verify-native-material-mapping.py`
