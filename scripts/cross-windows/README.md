# Experimental Windows x64 build from Linux

This is a local clang-cl/lld cross-build experiment, not a verified Windows release recipe.
It targets mPainter 0.9.0-alpha using the Microsoft C++ ABI, the pinned Blender 5.2
Windows libraries, and Qt 6.8.3 `win64_msvc2022_64`. See `~/mpainter-work/REPORT-win.md`
for the actual outcome and logs. No script pushes, publishes, or uploads files.

**Result, 4 November 2026:** the complete build stops at Windows-specific `QUuid`
assignment errors in the unchanged application input
`9b3975e988e5d31de31ac9c9708efe20be589a12`. No `mPainter.exe`, release ZIP or
release checksum was produced. Compiler and Qt probes passed under Wine; they
are diagnostic executables, not the application.

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
`build.sh` locks the Windows build directory. Ninja uses at most six jobs; the
Blender graph restricts heavy compile jobs to three and links to one. lld uses at
most six threads on fresh configurations. All Windows code runs under private
Xvfb; no host desktop display is used. Qt smoke tests explicitly select the
offscreen platform. GPU/OpenGL behavior is not tested by this widget probe.

`env.sh` rejects execution outside a container. `MP_WIN_WORK` can select another
simple absolute mount point. A matching **Linux Qt 6.8.3** installation must exist
at `$MP_WIN_WORK/toolchains/qt` for host moc/rcc tools; it was already available
on this machine. Downloads are pinned where possible and archive SHA-256 values
are checked. `msvc-channel.json` selects the original VS package manifest;
`fetch-manifest.py` verifies its observed SHA-256 (xwin skips that check because
Microsoft's channel hash differs from the downloaded manifest). The complete
original manifests remain in `cache/windows/xwin/dl` for acquisition provenance.
To reproduce this experiment after the scripts commit, set
`export MP_WIN_SOURCE_REF=9b3975e988e5d31de31ac9c9708efe20be589a12` before
`prepare-source.sh`. Its default is the current checkout's HEAD.

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

The project hook also enables Microsoft's `_HAS_DEPRECATED_SHARED_PTR_UNIQUE`
compatibility switch: the application uses `shared_ptr::unique()`, which the
Microsoft STL removes in C++20 by default. A future source port should replace
that obsolete API rather than rely on this switch.

The auxiliary target libraries are Assimp 5.4.3, HDF5 1.14.6 and LZ4 1.10.0,
built sequentially with clang-cl and the dynamic Microsoft CRT. GPU kernel
compilation is disabled, matching the existing Windows recipe. Packaging must
retain dependency license texts and runtime resources. `check-pe.py` checks all
staged PE imports, including recursive files and imported delay-load DLL names
reported by llvm-readobj. It does not prove dynamic plugin discovery or launch.
VC runtime DLLs are deliberately not classified as OS-supplied dependencies.

## Verified stopping point and follow-up

The final host compilation reports 16 ambiguous `QUuid = {}` assignments in
`SmartMaterialPreset.cc`, `TextureDocument.cc` and `MaterialLayeringStacks.hh`.
Qt's Windows headers add `operator=(const GUID&)`; an empty initializer can select
that overload or the UUID copy/move overloads. Explicit `id = QUuid{}` fixes the
minimal reproducer. This application source change has **not** been applied,
because the requested build input is the recorded HEAD. Further compile/link
failures may exist beyond this point.

Useful diagnostics, run sequentially inside the same container:

```sh
source scripts/cross-windows/env.sh
python3 scripts/cross-windows/verify-source.py
python3 scripts/cross-windows/verify-lfs.py
bash scripts/cross-windows/reproduce-uuid.sh
bash scripts/cross-windows/component-tests.sh
```

The UUID diagnostic requires the ambiguous variant to fail compilation and the
explicit variant to compile and run. Component tests use the original branding
and theme test targets and the Qt probe's runtime staging directory. These tests
can run after the full build fails; they do not depend on a linked application.
`smoke.sh STAGING_DIRECTORY` is reserved for a future completed application and
checks static imports, `--version` and `--cpu-workspace-shutdown-test`; there is no
application `--self-test` option. It has not been run against mPainter.

The existing `scripts/package-windows.ps1` remains the Windows packaging recipe;
this experiment has not reached installation, Qt deployment or packaging. Before
any future ZIP, resolve the source blockers, finish linking/installing, include
the runtime dependencies/resources/licenses, run closure and smoke checks, and
compute SHA-256 for the actual archive. Real Windows, GPU/OpenGL and tablet
validation remain outstanding.

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
