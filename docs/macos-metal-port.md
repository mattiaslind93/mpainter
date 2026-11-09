# macOS Metal port

The macOS Metal backend is integrated into the unified source tree. All platforms
build from the same revision using conditional compilation (`#ifdef __APPLE__`)
and CMake options (`BTS_MACOS_METAL_PROBE`, `BTS_MACOS_METAL_EDITOR`).

Mac-specific code resides in:
- `native/macos/` — Metal context, RHI bridge, probe diagnostic, shared contexts
- `scripts/macos/` — build, bundle, package and verification scripts
- `third_party/blender/patches/0002-macos-*.patch` through `0007-macos-*.patch`

The Linux and Windows OpenGL paths remain unchanged. Platform selection is implicit
via `#ifdef __APPLE__` guards in headers like `GpuProjectReadback.hh`,
`GpuReadbackFence.hh`, and the various `Gpu*` metadata adapters.

## Build and reproduce

Use Blender v5.2.0 `fbe6228777e7d9afefcd61a413844e790ae75db7`, its official arm64
libraries, Qt 6.8.3 with GuiPrivate, AppleClang and Ninja. The local scripts use a
workspace laid out as `~/mpainter-build/{mpainter,blender,build-metal,toolchains}`.
Assimp 5.4.3, HDF5 1.14.6 and LZ4 1.10.0 are pinned and built for arm64 by the
dependency script. No package-manager or system installation is performed.

```sh
export PATH="$PWD/../toolchains/python/bin:$PATH"
export GIT_CONFIG_GLOBAL="$PWD/../toolchains/gitconfig"
export GIT_CONFIG_NOSYSTEM=1
export TMPDIR="$PWD/../tmp"
mkdir -p "$TMPDIR"
# First establish the headless Blender graph, if not already built:
nice -n 15 python3 scripts/macos/build-probe.py \
  --blender ../blender --build ../build-metal \
  --qt ../toolchains/qt/6.8.3/macos --jobs 6
nice -n 15 bash scripts/macos/build-deps.sh
nice -n 15 bash scripts/macos/build-editor.sh
nice -n 15 python3 scripts/macos/verify.py \
  --build ../build-metal --output ../proof-metal-core-new
nice -n 15 python3 scripts/macos/make-editor-fixture.py ../fixture-new
nice -n 15 python3 scripts/macos/run-editor-test.py \
  --output ../proof-metal-editor-new -- \
  --workspace --gui-fix-demo '{output}' \
  --gui-fix-mesh "$PWD/../fixture-new/two_materials.obj" --gui-fix-resolution 256
```

Output directories must be new. Run one build at a time, at low priority, with
at most six build jobs. Keep Git configuration isolation for every invocation,
including Blender's updater. `prepare-blender.py` verifies patch receipts and
validates overlapping follow-up hunks on private copies without undoing the
live tree. All executable tests use Qt `offscreen` and workspace-local settings,
recovery, resources, caches and temporary directories.

For material tests use `--env 'BTS_NATIVE_MATERIAL_GPU_TEST={output}'` or
`--env BTS_NATIVE_FRAGMENT_NORMAL_TEST=1`, followed by
`-- --headless-viewport-test '{output}'`. The former variable is an output path,
not just a boolean. Bump tests use `BTS_NATIVE_FRAGMENT_BUMP_TEST=1`.
`BTS_LAYER_EDIT_DEMO=1` and `BTS_BAKE_FIX_DEMO=1` select editor workflows through
the `--gui-fix-demo` command. `verify-editor-evidence.py` checks their saved JSON,
project behavior and actual PNG/EXR headers/pixels; Pillow is used only for
screenshot comparison. Its EXR reader supports raw, ZIPS and ZIP scanlines.

## Rendering and shader changes

- On macOS `ViewportWidget` is a QWidget supplied by the Metal host. Linux keeps
  the QOpenGLWidget adapter. The real `BlenderHost::draw` uses windowless
  `MetalContextOwner`, EEVEE/UV render targets and the shared-device/queue QRhi
  bridge. There are no GL framebuffer blits or GL paint devices on the Mac path.
- Presentation currently **waits for a QRhi readback and paints a raster QImage**.
  This is a working editor path, not direct Metal swapchain presentation or a
  latency claim. Overlays and captures share this frame; Retina scaling uses
  device pixels. No visible-window/Retina/tablet test was performed.
- Native material compute/fragment callbacks now call Blender's raw-GLSL
  conversion API. This is required for out/inout references, matrix casts and
  built-in metadata; callbacks otherwise bypass the Python shader constructor's
  conversion. Native authoring shaders disable fast math/contraction on Metal.
  The MSL compatibility patch supplies `roundEven` and supports generated
  `precise` declarations. The fragment cache distinguishes tangent-space output.
- GL VAO, sRGB and query state is guarded. Blender GPU state owns Metal raster
  state and uploads. Metal frame readiness uses nonblocking storage completion;
  GL timer queries remain on GL. Metal GPU elapsed times report unavailable.
  Shared Metal system contexts support the headless runtime; hosted compiler
  worker mode has not been validated.
- Save readback uses frozen texture views and four bounded 4 MiB transfer packets.
  Completed float data is compacted when uniform, preserving logical dimensions
  required by native-source proxy images. Compaction currently scans downloaded
  pixels; large-project performance has not been established.

## Evidence and boundaries

The editor acceptance checks include real 3D/UV strokes, cancellation, channel
changes, undo/redo, visibility, save/open, another edit/save, failed save/open
preservation and a second save/open with exact stored GPU pixel hashes. CPU
replay cache hashes change on the first save/open at the authored channel
precision; rendered screenshot differences are measured separately. Do not call
that first roundtrip bit-exact CPU replay.

Layer editing performs 23 operations and checks CPU/GPU composition. AO and
curvature bake at 512 square, eight rays, survive undo/redo and save/open.
Four export encodings are decoded independently, including signed EXR height,
PNG clamping, scalar precision, selected-set output and preserved document/view.
The native corpus covers generated parameters, owner/UV/object/view contexts,
image/ramp resources, mapping, archive hydration, resident geometry, normal
stacks and fixed numerical oracles. Some fragment diagnostics deliberately do
not claim production admission or complete Blender material parity.

The ten component regressions include brush/polygon references, asynchronous
readback/COW/cancellation, texture views, BVH rays, CPU data safety and Cycles CPU
and actual Metal rendering. Cycles background rendering does not prove the
editor's interactive Cycles controls.

## Local packaging

`bundle-editor.py --output ../dist/mPainter.app` assembles the real host, Qt,
Blender 5.2 resources/Python/color management/studiolights, native dependencies
and runtime assets. OIDN CPU/Metal device plugins are also deployed beside the
core library in Frameworks for dynamic discovery. It audits architecture and load-command closure, rewrites
local rpaths and applies ad-hoc code hashes without a signing identity or
notarization. `--resume` is only for refreshing a workspace staging bundle.
Use `run-editor-test.py --bundled --executable .../Contents/MacOS/mPainter` to
exercise resource discovery without build-tree resource environment overrides.
The local packaging helper requires successful bundled tests before making a
DMG. Nothing in this process uploads or publishes an artifact.

Still required: direct asynchronous Metal presentation, large-project memory
and interaction measurements, visible/manual Retina/tablet/resize/recovery and
interactive Cycles tests, and real Linux/Windows builds/regressions. Their GL
source paths are retained; this Mac is not runtime evidence for those platforms.
Only Apple M2 Max on macOS 27.0.1 has been exercised. Deployment targets are
Blender 11.2 and the Qt editor 12.0; older macOS versions remain untested. CI and
release publication have not been enabled or run by this port work.
