# Experimental Windows x64 build from Linux

This is a local clang-cl/lld cross-build, not yet a verified Windows release recipe;
[the lessons below](#lessons-from-the-verified-092-build) record what has been verified and the known gaps.
It targets the mPainter version in `pyproject.toml` using the Microsoft C++ ABI, the pinned Blender 5.2
Windows libraries, and Qt 6.8.3 `win64_msvc2022_64`. See `~/mpainter-work/REPORT-win.md`
and `~/mpainter-work/REPORT-win2.md` for the actual outcomes and logs. No script pushes, publishes, or uploads files.

The first attempt stopped at ambiguous UUID assignments. The follow-up source port
uses explicit UUID resets (`id = QUuid()`, never `id = {}`; `build.sh` runs
`scripts/host/check-windows-uuid-resets.py` on the snapshot before compiling), removes obsolete shared-pointer APIs, and addresses
Windows file replacement, compiler checks and dependency discovery. The follow-up linked and installed the application, passed Wine CPU smoke and
component checks, and captured its main window under Xvfb/llvmpipe. This still
does not establish a verified real-Windows release.

MinGW-w64 and llvm-mingw use a different C++ ABI and cannot substitute for the
MSVC-built Qt, OpenImageIO, USD and other C++ dependencies. Rebuilding that entire
stack would be a separate port; Blender's pinned Windows platform file also
rejects non-MSVC-compatible compilers. clang-cl has an explicit supported code path
in that file, but Linux-hosted Windows builds need additional build-graph work.

## Container and limits

The experiment uses the existing Fedora 43 `bts-build` distrobox. The host remains
unchanged. The container's original memory limit was unlimited; it is now capped
at **30 GiB, no swap**, below the session's 34 GiB scope limit. Downloads, compiler
outputs, Wine prefix, package caches and logs belong on the large work volume.
Container packages themselves consume about 2 GiB in Podman's existing storage.

From the host, inspect available space before proceeding:

```sh
df -h ~/mpainter-work /var/home/mlind
podman update --memory 30g --memory-swap 30g bts-build
distrobox enter bts-build
```

Inside the container, create the simple mount only if it is not already mounted:

```sh
sudo mkdir -p /mnt/mpainter-phase1
mountpoint -q /mnt/mpainter-phase1 || sudo mount --bind "$HOME/mpainter-work" /mnt/mpainter-phase1
cd /mnt/mpainter-phase1/mpainter
bash scripts/cross-windows/bootstrap.sh
bash scripts/cross-windows/prepare-source.sh
bash scripts/cross-windows/probe.sh
bash scripts/cross-windows/qt-probe.sh
bash scripts/cross-windows/build.sh
```

Run these **sequentially** and do not run another heavy build/test concurrently.
`build.sh` locks the Windows build directory. Set `MP_WIN_JOBS=2` for a lower-memory rebuild (accepted range: 1–6, default: 6).
Ninja uses at most six jobs; the
Blender graph restricts heavy compile jobs to three and links to one. lld uses at
most six threads on fresh configurations. All Windows code runs under private
Xvfb; no host desktop display is used. CPU smoke tests select the offscreen
platform. The optional main-window capture uses software OpenGL separately;
neither test validates physical graphics hardware.

`env.sh` rejects execution outside a container. `MP_WIN_WORK` can select another
simple absolute mount point. A matching **Linux Qt 6.8.3** installation must exist
at `$MP_WIN_WORK/toolchains/qt` for host moc/rcc tools; it was already available
on this machine. Downloads are pinned where possible and archive SHA-256 values
are checked. `msvc-channel.json` selects the original VS package manifest;
`fetch-manifest.py` verifies its observed SHA-256 (xwin skips that check because
Microsoft's channel hash differs from the downloaded manifest). The complete
original manifests remain in `cache/windows/xwin/dl` for acquisition provenance.
`prepare-source.sh` defaults to the current checkout's HEAD. To reproduce the
original failed attempt, set `MP_WIN_SOURCE_REF` to the revision in `REPORT-win.md`.

## Source and build adaptations

`prepare-source.sh` pins Blender via `scripts/prepare-blender.py`, fetches the
Windows library Git commit recorded by that source tree, and downloads its LFS
payloads (excluding separate debug directories and PDBs). It fetches only Blender's
`release/**` LFS runtime files, avoiding the unused regression asset corpus. The
application is snapshotted with `git archive HEAD` and its revision is saved to
`build/windows/source-head.txt`. It does not build an older installed application.

`prepare-cross.py` changes only the isolated Blender build copy: it avoids native
Visual Studio registry discovery, uses Linux Python for generators, and wraps
Windows generator executables in Wine. `project-hooks.cmake` evaluates nested
link-library generator expressions in a diagnostic target; otherwise Ninja
cannot even parse the generated graph. `fix-library-case.py` adds case aliases to
the downloaded SDK/libraries and relocates OIIO's hardcoded Windows build path.
These adaptations preserve the application's C++ source. Recreate the isolated
source/library directories if you need a pristine preparation; do not run the
upstream updater over a patched checkout. Changing the toolchain may require
`configure.sh --fresh` before rebuilding.

`snapshot-source.py` refreshes the application snapshot from a committed revision
without touching unchanged file timestamps. It verifies the old snapshot against
Git first and refuses source drift or untracked-file collisions. Run it with
`env.sh` sourced after committing source fixes, then rerun `build.sh`.
The source uses `use_count() == 1`; no deprecated-STL compatibility switch is needed.
An additional Blender patch uses explicit ANSI Win32 functions for its `CHAR`
crash-report buffers, including when Qt enables Unicode for the host target.

The auxiliary target libraries are Assimp 5.4.3, HDF5 1.14.6 and LZ4 1.10.0,
built sequentially with clang-cl and the dynamic Microsoft CRT. GPU kernel
compilation is disabled, matching the existing Windows recipe. Packaging must
retain dependency license texts and runtime resources. `check-pe.py` checks all
staged PE imports, including recursive files and imported delay-load DLL names
reported by llvm-readobj. It does not prove dynamic plugin discovery or launch.
By default every non-OS import must resolve. Packaging/smoke explicitly enable
`--allow-optional-gpu-drivers`, which reports only the OIDN CUDA/HIP module's
external `nvcuda.dll`/`amdhip64_7.dll` imports separately. These dependencies are
not bundled or verified; they are never exempted for the core application.
VC runtime DLLs are deliberately not classified as OS-supplied dependencies.

## Verification and local packaging

Run diagnostics sequentially inside the same container:

```sh
source scripts/cross-windows/env.sh
python3 scripts/cross-windows/verify-source.py
python3 scripts/cross-windows/verify-lfs.py
bash scripts/cross-windows/reproduce-uuid.sh
bash scripts/cross-windows/component-tests.sh
```

The UUID diagnostic requires the ambiguous variant to fail compilation and the
`QUuid()` variant to compile and run. Component tests run branding, theme,
Unicode project save/backup/conflict handling, and signed warp-tangent dragging
under Wine. They use the Qt probe's runtime staging directory and do not depend
on a linked application. Failed checks stop the script.

After a completed build and install:

```sh
python3 scripts/cross-windows/package.py
version=$(sed -n 's/^version = "\(.*\)"/\1/p' pyproject.toml | head -1)
bash scripts/cross-windows/smoke.sh "$MP_WIN_WORK/dist/mPainter-$version-windows-x64"
bash scripts/cross-windows/capture.sh "$MP_WIN_WORK/dist/mPainter-$version-windows-x64"
```

`package.py` stages runtime resources, Qt plugins, release CRT and dependency
licenses, checks required static DLL closure (with explicit optional GPU imports), and writes the ZIP plus its
`.zip.sha256` file locally. It refuses to overwrite existing outputs. The package is
named after the `pyproject.toml` version of the built snapshot; packaging stops if
this checkout is at another version, if `mPainter.exe` carries a different VERSIONINFO
(`pe_version.py` reads it), or if a Mesa software-rendering DLL such as
`opengl32.dll` or `libgallium_wgl.dll` was staged. Keep those in VM test folders only. `smoke.sh`
checks static imports, `--version` and `--cpu-workspace-shutdown-test`; there is
no application `--self-test` option. `capture.sh` attempts the main window using
Wine's Windows Qt plugin inside private Xvfb and software OpenGL, saving
`$MP_WIN_WORK/win.png`. It selects GLX for mPainter in the isolated Wine prefix
because Wine's EGL device selection conflicts with forced software rendering.
Capture can fail even when CPU smoke tests pass.

The existing `scripts/package-windows.ps1` remains the native Windows packaging
recipe. Real Windows/NTFS durability, GPU drivers, tablet input and interactive
painting must be validated separately. Wine checks cover only the paths actually
exercised; see the dated local report for evidence and remaining blockers.

## Lessons from the verified 0.9.2 build

On 8 November 2026, commit `bf17b14` completed the full cross-build in `bts-build`.
`mPainter.exe` carried FileVersion and ProductVersion 0.9.2, and `source-head.txt`
matched the commit. `package.py` produced a ZIP with SHA-256
`44668e92ea03c7ffd7f726b6d8169be0fecfd702a0cff0eb0a8f0d5ce2c1e8f0`. In a Windows 11
QEMU/KVM VM, with Mesa llvmpipe copied into a separate test folder, the package
started, showed 0.9.2 in About, painted, hid and added layers, saved and reopened a
project, exported textures and shut down cleanly. No real GPU was tested. The ZIP
contained no Mesa DLL, and `mPainter.exe` did not import `tbbmalloc_proxy.dll`
(the DLL file itself is still shipped).

**QUuid resets.** Windows Qt declares `QUuid(const GUID &)` and
`QUuid &operator=(const GUID &)` under `Q_OS_WIN` in `quuid.h`. `GUID` is an
aggregate, so `id = {}` matches both assignment operators and fails under
clang-cl/MSVC, while Linux and macOS accept it. This stopped the build at
`native/host/Res8kWitness.inc` (Ninja step 207/252). Write `id = QUuid()`.
Declarations such as `QUuid id{}` or `QUuid id = {}` do not assign and stay safe.
`scripts/host/check-windows-uuid-resets.py` runs in `build.sh` before configuring,
in `verify-cpu.py` and in the Windows CI job; `reproduce-uuid.sh` demonstrates the
ambiguity against the real Windows Qt headers. When the checker misses a pattern,
add a fixture to `test-windows-uuid-resets.py` and extend the checker; never bypass it.

**Version.** `pyproject.toml` is the only version source. CMake generates
`mpainter-version.h` for `packaging/mpainter.rc`, and `package.py` derives
`mPainter-<version>-windows-x64`. It refuses a checkout/snapshot version mismatch
and an `mPainter.exe` whose VERSIONINFO differs (read by `pe_version.py`). Before
handing off a package, confirm that `source-head.txt` is the intended commit and
check the PE version, the ZIP name, its `.zip.sha256` file and the About dialog.
Do not hardcode a version in scripts or examples.

**Dependencies.** A complete DLL closure does not prove that the process starts:
importing `tbbmalloc_proxy.dll` from `mPainter.exe` crashed before `main()`
(fixed in `de6fa19`). `prepare-cross.py` keeps the proxy out of the host link and
`check-pe.py` rejects the import. `package.py` refuses Mesa software-rendering
DLLs. Copy `opengl32.dll`, `libgallium_wgl.dll` and similar files only into a
separate VM test copy, never into the package stage or `dist` folder.

**Incremental rebuilds.** `prepare-source.sh` is first-time preparation. Afterwards
`prepare-cross.py` has patched the Blender copy and `fix-library-case.py` has added
case aliases to the library checkout, so rerunning it refuses or mixes states. To
build a new commit in a prepared workspace, run inside the container:

```sh
source scripts/cross-windows/env.sh
python3 scripts/cross-windows/snapshot-source.py --ref <commit>
python3 scripts/cross-windows/verify-source.py
MP_WIN_JOBS=2 bash scripts/cross-windows/build.sh
python3 scripts/cross-windows/package.py
```

Use a fresh workspace when a pristine preparation is needed.
`scripts/host/test-windows-release-gates.py` (run by `verify-cpu.py`) fails if the
UUID check, the `tbbmalloc_proxy.dll` rejection or the Mesa DLL refusal is removed,
or if a script here hardcodes a package version.

**Evidence levels.** Wine/Xvfb checks (`component-tests.sh`, `smoke.sh`,
`capture.sh`), a real Windows VM with test-only software OpenGL, and Windows on
real GPU hardware are separate results; none implies the next. Report each level
separately, together with the known gaps.

Known gaps at `bf17b14`: the Qt TLS plugin `tls/qschannelbackend.dll` is not
packaged, so HTTPS material downloads fail in the app. The fill-transform assertion
seen under Wine, the branding hang and the duplicated Help menu remain open. Real
GPU and tablet behavior were not tested, so this is not a verified release.

## Microsoft SDK and runtime terms

xwin is MIT/Apache-2.0; **Microsoft payloads are separately licensed**. Its
`--accept-license` option follows a link to [the VS 2019 build-tools terms](https://visualstudio.microsoft.com/license-terms/mlt031519/),
which is not a blanket redistribution grant or proof of VS 2022 licensing.
The [Windows SDK terms](https://learn.microsoft.com/en-us/legal/windows-sdk/license)
allow development/build uses for Microsoft-platform programs and restrict
redistribution to listed distributable code. They require notices and applicable
end-user protections; `.lib` redistributables are distributed only as linked
results. They restrict non-Microsoft-platform redistribution and putting Microsoft
code under an excluded source-sharing license. Do not distribute the SDK tree,
headers, build tools, debug CRT or xwin cache as application content.

The [VS 2022 redistribution list](https://learn.microsoft.com/en-us/visualstudio/releases/2022/redistribution)
is conditional on the applicable valid license and authorizes listed unmodified
files subject to those terms. Consult also the [runtime terms](https://visualstudio.microsoft.com/license-terms/vs2022-cruntime/).
`fetch-crt.py` acquires the x64 **release** CRT VSIX identified by the retained
Microsoft manifest for local tests; it does not establish redistribution rights.
The local experiment does not distribute anything or certify the owner's license
entitlements. SDK/CRT contents must not be relicensed as part of mPainter's GPL
source. Local license snapshots are retained under `logs/windows/`.
