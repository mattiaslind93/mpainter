# Building mPainter 0.9.0-alpha

The native host is compiled inside Blender's CMake graph. The stable internal target is `bts-host`; its installed executable is `mpainter` on Linux and `mPainter.exe` on Windows. Blender's standalone executable is built only to complete its runtime resource installation and is excluded from the mPainter package.

## Sources and prerequisites

Use Git with Git LFS, Python 3.11+, CMake 3.31+, Ninja, `file` and `patchelf` for Linux packaging, a C++20 compiler, Qt **6.8.3** (including private GUI headers), Assimp and HDF5 development packages. Use a large local build volume; allow at least 20 GB for source, libraries, build and packaging. The public repository contains the application and an integration patch, not the full Blender source or a material library.

```sh
python3 scripts/prepare-blender.py ../build/blender
cd ../build/blender
python3 build_files/utils/make_update.py --no-blender
```

This is Blender's official library updater, the same mechanism used by `make update`. `--no-blender` preserves the pinned release and local patch. It fetches the platform's precompiled libraries via Git LFS. Do not run a source-updating command over the patch. The release tag is checked against `fbe6228777e7d9afefcd61a413844e790ae75db7`.

Official platform instructions: [Linux](https://developer.blender.org/docs/handbook/building_blender/linux/), [Windows](https://developer.blender.org/docs/handbook/building_blender/windows/) and [macOS](https://developer.blender.org/docs/handbook/building_blender/mac/).

## Linux / Bazzite

Use a development distrobox with the compiler and development packages; the immutable host need not be changed. The phase-one build used the existing Fedora 43 `bts-build` container, GCC 15.3.1, Qt 6.8.3, and Blender's pinned Linux libraries. New sources, libraries, build directories and caches were placed on the work volume. The original working source and toolchains were only read/copied.

From this repository, with `QT_ROOT_DIR` pointing to the installed Qt SDK:

```sh
python3 scripts/build.py --blender ../build/blender --build ../build/native --qt "$QT_ROOT_DIR" --cuda "$CUDA_TOOLKIT_ROOT_DIR" --optix "$OPTIX_ROOT_DIR" --jobs 4
python3 scripts/package-linux.py --runtime ../build/native/bin --qt "$QT_ROOT_DIR" --output ../dist
python3 scripts/package-appimage.py --appdir ../dist/mPainter-0.9.0-alpha-linux-x86_64 --output ../dist/mPainter-0.9.0-alpha-linux-x86_64.AppImage --cache ../cache/appimage
../dist/mPainter-0.9.0-alpha-linux-x86_64.AppImage
```

The AppImage wrapper uses checksum-pinned appimagetool 1.9.1 and type2 runtime 20251108. It can extract and run when FUSE is unavailable (`--appimage-extract-and-run`); extraction must have writable temporary storage. It does not lower the required glibc version.

In the tarball, `AppRun` is the launcher and `mpainter` is the actual ELF executable.

The tarball bundles Qt and dynamically resolved dependencies, runtime scripts, color management data and studio lights. It requires a compatible glibc, the system graphics driver, X11/XWayland and OpenGL 4.3. A package built on Fedora 43 is not a universal older-Linux package. The hosted Ubuntu build has a different glibc baseline; it must also be launch-tested before publication. Linux releases include CUDA kernels for sm_86 (RTX 30 series), forward-compatible compute_75 PTX, and OptiX kernels. Building these requires a CUDA toolkit and OptiX 8.0 or newer SDK headers; pass their roots with `--cuda` and `--optix`. The verified build uses CUDA 13.2 and [NVIDIA OptiX 9.1 headers](https://github.com/NVIDIA/optix-dev/tree/v9.1.0). Cycles selects OptiX, then CUDA, and falls back to CPU when no compatible GPU is available. The launcher resolves host NVIDIA drivers, including the `/run/host` driver mount in Distrobox, without packaging driver binaries. The Linux package omits optional Intel Level Zero and AMD HIP denoising backends; CPU denoising remains included. Its CUDA denoising plugin requires the system NVIDIA driver (`libcuda.so.1`), which is deliberately not bundled.

A drive with literal backslashes in its mount-point name exposes a CMake path-normalization problem. In the development container, bind-mount the work directory to a simple path on the same volume, configure/build through that path, and unmount it after finishing. This does not copy sources onto the home partition.

Capture a real application window without a desktop screenshot utility:

```sh
BTS_BRANDING_CAPTURE="$PWD/screens" BTS_BRANDING_QUIT=1 ./AppRun
```

The application saves its splash and main-window widget captures and exits through Qt's event loop. These are application captures, not a full GNOME desktop screenshot. Test with isolated `XDG_CONFIG_HOME`, `XDG_DATA_HOME`, `XDG_CACHE_HOME` and `BTS_RECOVERY_DIR` to avoid modifying an artist's preferences or recovery files.

## Windows x64

Use Visual Studio 2022 Build Tools with the Desktop C++ workload, Ninja, Git LFS, Python, and Qt 6.8.3 `win64_msvc2022_64`. Install Assimp and HDF5 with vcpkg's `x64-windows` triplet. Pass its CMake toolchain:

```powershell
python scripts/build.py --blender ../build/blender --build ../build/native --qt $env:QT_ROOT_DIR --toolchain "$env:VCPKG_INSTALLATION_ROOT/scripts/buildsystems/vcpkg.cmake" --jobs 3
Copy-Item "$env:VCPKG_INSTALLATION_ROOT/installed/x64-windows/bin/*.dll" ../build/native/bin
./scripts/package-windows.ps1 -Runtime ../build/native/bin -Qt $env:QT_ROOT_DIR -Output ../dist
```

The package includes the branded Windows executable/icon, Qt deployment and runtime DLLs. The local clang-cl cross-build has linked and installed successfully, passed Wine CPU startup/workspace checks, and captured the main window with llvmpipe in Xvfb. Required DLL imports resolve; optional OIDN CUDA/HIP modules still require external driver/runtime DLLs. Native Windows/MSVC, hardware graphics and tablet behavior remain unverified. CI is a build recipe, not independent evidence of a successful Windows release.

An experimental Linux-hosted Windows build is documented in
[`scripts/cross-windows/README.md`](../scripts/cross-windows/README.md). It uses
clang-cl/lld, an xwin-acquired Microsoft SDK/CRT, the exact Blender Windows library
revision, Qt's MSVC package with matching Linux host tools, and separately built
Assimp/HDF5/LZ4. Compiler/component probes and the completed application have run
headlessly under Wine/Xvfb; these checks do not establish a real-Windows release. The scripts
record the required case-sensitive filesystem and generator adaptations, resource
limits, static DLL checks, and Microsoft redistribution terms. Keep all builds and
tests sequential and inside a capped container; do not publish this experiment.
The initial 4 November 2026 attempt stopped at ambiguous UUID assignments. The
follow-up port replaces those assignments and deprecated shared-pointer APIs,
adds Windows project-save handling, and extends the sequential Wine checks.
`~/mpainter-work/REPORT-win2.md` records the follow-up's actual build, artifact
and verification outcome; the scripts alone are not release evidence.

## macOS arm64

The macOS build uses the Metal backend and a Qt RHI presentation bridge. It builds from the same source tree as Linux and Windows, with platform-specific code guarded by `#ifdef __APPLE__` and CMake `if(APPLE)` conditions.

### Prerequisites

- Xcode Command Line Tools or full Xcode
- Qt 6.8.3 arm64 (including private GUI headers)
- CMake 3.31+, Ninja, Python 3.11+
- Assimp and HDF5 development packages (brew or manual)
- Blender's official arm64 libraries (fetched by `make_update.py`)

### Building

```sh
# Prepare Blender with macOS patches
python3 scripts/prepare-blender.py ../build/blender
cd ../build/blender
python3 build_files/utils/make_update.py --no-blender
cd -

# Build the probe (diagnostic, headless Metal validation)
python3 scripts/macos/build-probe.py --blender ../build/blender --build ../build/probe --qt "$QT_ROOT_DIR"
scripts/macos/run-probe.sh ../build/probe ../probe-output

# Build the editor
scripts/macos/build-editor.sh  # Configure with BTS_MACOS_METAL_EDITOR=ON
scripts/macos/bundle-editor.py --build ../build/editor --qt "$QT_ROOT_DIR" --output ../dist/mPainter.app
scripts/macos/package-editor.py --app ../dist/mPainter.app --output ../dist
```

The Metal probe (`mpainter-metal-probe`) is a diagnostic target that validates GPU operations, brush raster parity, viewport rendering and Qt RHI bridge integration in headless mode. It is not the editor.

The editor is built with `-DBTS_MACOS_METAL_EDITOR=ON` and links Qt Widgets. The bundle includes Blender's runtime resources, OIDN device plugins, and denoiser ML models.

### Signing and notarization

For distribution, sign the app bundle with your Developer ID and notarize with Apple's `notarytool`. For local testing, Gatekeeper may require Finder > right-click > Open. The quarantine attribute can be removed with `xattr -dr com.apple.quarantine /path/to/mPainter.app`.

See [docs/macos-metal-port.md](macos-metal-port.md) for technical details of the Metal/Qt RHI integration.

## CI and publication

`.github/workflows/release.yml` runs on `v*` tags and manual dispatch with Windows, macOS arm64 and Linux jobs. It uses GitHub-hosted runners, a six-hour timeout, three build workers, early disk cleanup, shallow upstream source, cached platform libraries, Qt 6.8.3 from `install-qt-action`, and CUDA/OptiX kernels. Runners must provision CUDA and OptiX SDKs before invoking the build script. Library caches reduce network cost but cannot guarantee hosted disk space or build duration. Windows and Mac builds can exceed the nominal 14 GB budget; monitor runner disk usage on the first run.

Each successful build uploads its platform archive. A tagged run creates a draft release via `softprops/action-gh-release` only if the full matrix passes. The known Mac blocker currently prevents that release step. Manual dispatch builds artifacts without publishing. No workflows were run remotely during phase one, and no repository, remote, tag or release was created.

Before publishing, run the workflow, resolve platform failures, test the actual downloaded artifacts, retain dependency license texts and provide corresponding source. Version 0.9.0-alpha is fixed in the package filenames and metadata; update all version metadata together for a later release.

Runtime packaging normalizes compiled OSL source-path metadata and gives legacy wide-gamut color spaces neutral display names. Standard interoperability aliases remain intact so embedded image metadata keeps its color meaning. These aliases and third-party codec/license references are included in the audit, rather than silently altering dependency APIs.

For the focused release regressions, build `bts-cpu-check` in the configured native build. This runs branding, shared dark-theme rendering/contrast and empty-library persistence tests. Renamed executable/session detection and generated sphere geometry were also checked in the phase-one local validation. The build also runs the retained GPU/CPU-path source regression check. These checks do not replace end-to-end painting, tablet and platform validation. The full inherited diagnostic suite has not been migrated or validated for an empty bundled library; some old asset-dependent proofs require updated fixtures before they can be used as release gates.
