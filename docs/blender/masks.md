# Blender masks and Material Presets

The bridge is a local command-line workflow, with stock Blender shader groups and
mPainter's production archive codecs. It does not install an add-on or change the
existing shelf UI. `assets/blender/Shader_Ball_JL_01-mPainter.blend` is the prepared
Blender asset; the scripts also reproduce it from the current built-in recipes.

## Native model and storage

| Concern | Implementation / contract |
|---|---|
| Mask state | `native/host/LayerMask.hh`: background, enabled state, sparse scalar paint, ordered effects. Channel 2 holds premultiplied scalar data and coverage. |
| Effect model | `MaskEffect.hh`: Color Selection, Fill, Levels, Generator, Compare, Paint, Filter, Anchor, Group. JSON includes UUID, enabled state, type-specific settings and optional display name. |
| Evaluation | `LayerMask::value` evaluates the stored vector from first to last (UI bottom to top); starts with paint over background, blends effects, clamps the final result to [0,1]. Generators return no value when required inputs are absent, preserving the preceding result. |
| Blend | `LayerBlend.hh`: `lerp(previous, blend_value(mode, source, previous), opacity)`. Groups start from black and blend their isolated result. Levels act on the preceding result. |
| CPU generator | `MeshGenerator.hh`: `generated_mask`, map sampling, surface proxies, anchors, normal decoding, native noise and final shaping. |
| GPU evaluation | `GpuMaskStages.hh`, `GpuLayerEffects.hh`, `GpuMaskResolve.hh`: stage construction, separate paint/spatial/anchor/compare passes and cached GPU prefixes. The new proof measures the production **CPU** evaluator against stock Blender Cycles; it does not claim a new GPU/UI regression proof. |
| Mask archive | `.btsmask`, `MaskPreset.hh`: qCompress (big-endian uncompressed-length prefix + zlib), little-endian body magic `0x42544d31`, JSON length/metadata, sparse tiles/pages, effect paint, texture resources. Writer version 6; reader accepts 1–6. Qt 6.8 stream, float32 raster data. Metadata contains background/effects and texture descriptors/bindings; standalone archive does not store mask enabled state. |
| Embedded textures | `MaskPresetTextures.hh`: deduplicated decoded float32 RGBA, dimensions, identity/hash, raw/color-space metadata, optional UDIM members, bindings to effect UUIDs. No dependence on original grunge file paths. |
| Material Preset | `SmartMaterialPreset.hh/.cc`: detached `TextureSet` folder subtree. `.btsmaterial` uses `BTSHIST2` independently zlib-compressed blocks, little-endian body magic `0x42534d31`, version-1 JSON with a `TextureDocument` (current schema 79), sparse layer/mask/content paint and shared textures. |
| Material capture/apply | Capture includes all folder descendants, validates internal anchors, strips runtime mesh maps/projection/skew/native layering state. Apply renews layer/effect/anchor IDs, remaps internal references, validates map/tile/channel dependencies, and installs atomically. Destination mesh maps drive the same preset on another object. External anchors must be resolved before capture. |
| Library | `AssetCatalog.cc`, `main.cc`: built-ins are compiled Qt resources and in-memory assets; user shelf is `QStandardPaths::AppDataLocation/shelf` (or `--library-dir`), with `library.json` metadata and content-addressed `blobs/<hash>`. Project-retained assets have separate identities. Import/export in the shelf already supports `.btsmask` / `.btsmaterial`. |

## Materials vs Material Presets

**Materials** contains plain materials without dynamic mask generators. This
includes Poly Haven, procedural built-ins, CraftyPack, imported Blender materials,
and saved layer folders that contain only paint, bitmap masks, or ordinary effects.

**Material Presets** contains materials with dynamic smart masks that use the same
generators as **Mask generators**. A generator anywhere in a saved folder qualifies,
including generators inside nested mask groups or material channel effects.
Disabled generators and hidden layers still count because they remain editable
parts of the saved material. The two shelves never show the same resource.

Classification comes from decoded content, not filenames, display names, origin,
or user tags. Saving a plain folder places it in **Materials**. Saving a folder
with generators places it in **Material Presets** and preserves its dynamic masks.
Existing user libraries and project resources receive the same classification
when loaded. Original archive bytes, resource IDs, and hashes stay unchanged.

Both kinds of editable layer-folder recipes use `.btsmaterial` archives. The
legacy `smartmaterial` usage identifies this archive and its application path;
it no longer determines shelf membership. Original `.spsm` archives use a
metadata-only reader to classify their native generator content before full
worker decoding. Classification does not decode image or compiled package
payloads. The cached result is bound to the source bytes and interpretation
policy and is invalidated when either changes.

Imported Blender shader trees use the separate native-material workflow
(`BlendMaterial`, `NativeMaterialGraph`, and `NativeMaterialArchive`). Their
procedural shader nodes do not themselves count as smart masks. This bridge
creates native layer-folder archives.

## Mesh maps and coordinate conventions

| Role ID | Native display name | Blender group | Bundled filename |
|---|---|---|---|
| 0 | Normal | not needed by these masks | tangent normal, not bundled here |
| 1 | World space normal | `mPainter.Input.World Normal.*` | `shaderball-jl01-map-1.png` |
| 2 | ID | `mPainter.Input.ID.Closest` | `shaderball-jl01-map-2.png` (new raster) |
| 3 | Ambient occlusion | `mPainter.Input.AO.*` | `shaderball-jl01-map-3.png` |
| 4 | Curvature | `mPainter.Input.Curvature.*` | `shaderball-jl01-map-4.png` |
| 5 | Position | `mPainter.Input.Position.*` | `shaderball-jl01-map-5.png` |
| 6 | Thickness | `mPainter.Input.Thickness.*` | `shaderball-jl01-map-6.png` |

The source OBJ's vertices, polygon loop UVs and axes are preserved; no unwrap,
repack, axis conversion or welding is performed. Maps are linear **Non-Color**,
packed float images. The five original 512² PNGs are resampled by mPainter's
`shaderball_uv_surface(256)` into the reference 256² working grid. Blender image
datablocks retain the established map-number filenames. `World Normal` decodes
as `2*RGB-1`, Position as `2*RGB-1` for the preview surface with extent 2.
Curvature is neutral at 0.5, convex above, concave below. AO is unoccluded at 1.
The first component supplies scalar inputs. Float buffers have bottom-left origin;
PNG display rows are flipped on output.

Input groups quantize UVs to this working grid's texel centers, matching native
mask raster evaluation and avoiding barycentric perturbations of nearly-zero
normals. This is a fixed reference grid, not resolution-independent filtering.
The shader groups do not use Blender's Geometry Normal as a substitute for the
baked World Normal. Map inputs are linked through explicit UV Map nodes.

There is no shipped ID bake. The base file rasterizes the original OBJ material
assignments into UV space: this OBJ has one ID, represented as red; alpha marks
covered UVs. It is labeled as a generated material-ID map, not a recovered bake.
`mPainter.Mesh Map Inputs` exposes all six map groups. The ten built-ins do not
use ID selection.

## Low-level generator parameters

IDs are stable: 0 None, 1 World Height, 2 Curvature, 3 Ambient Occlusion,
4 Thickness, 5 Dirt, 6 Light (World Normal), 7 Native Grunge, 8 Native Edge Wear,
9 Native Cavity Dirt. There are **nine active generator kernels**, distinct from
the **ten shelf recipes** below.

Common controls: axis 0–2, curve 0 linear / 1 smoothstep / 2 square,
map_filter 0 nearest / 1 bilinear, contrast 0–8, bias -1–1, invert,
dirt_curvature 0–1, light_direction (three components -1–1).
`native_grunge` stores nine values: amount, scale, seed, pattern, AO influence,
curvature influence, Position influence, Normal influence, Thickness influence.
Amount/influences are 0–1; scale .01–4096; seed 0–65535; pattern 0–3.
Optional settings include response 0 legacy / 1 support preserving / 2 gravity
leaks / 3 moss / 4 independently composed sources, curvature_radius 0–64,
`grunge_projection`, `surface_fallback`, `optional_inputs`, and two input anchors.

Shaping usually computes `clamp((input-.5)*contrast+.5+bias)`, then invert and
curve. Response 4 adds bias before contrast. Support-preserving edge/cavity
responses retain zero support. Light normalizes both normal and light vectors
with the native validity thresholds. Edge Wear extracts positive curvature;
Cavity Dirt combines inverse AO with negative curvature. Dirt uses
`(1-AO)*(1-dirt_curvature*Curvature)`. Auxiliary influences modulate their results.
Missing-map surface proxies are explicitly provisional, not baked geometry data.

## The ten shelf groups

Built from `BuiltinSmartMasks.cc`, not copied parameter guesses or screenshots.
Stable filenames/UUID inputs use the legacy slugs in `BuiltinMaskNames.hh`.
All eight detailed recipes end with triplanar bitmap Multiply and Levels
(input .06–.94, output 0–1, clamp); the two simple masks have one generator.

| Shelf / Blender group | Stable slug | Base generator | Contrast / bias | Grunge / opacity / 3D scale / gamma |
|---|---|---|---|---|
| Rust | oxide-flecks | Cavity Dirt | 1.8 / -.05 | rust_spots / .72 / .28 / .85 |
| Edge Wear | exposed-edges | Edge Wear | 2 / .20 | edge_wear / .65 / .34 / .9 |
| Crease Dirt | crevice-sediment | Cavity Dirt | 2 / .3 | crease_dirt / .8 / .32 / .82 |
| Moss | organic-growth | Cavity Dirt | 1.4 / -.12 | moss_organic / .78 / .42 / 1.15 |
| Ground Dirt | ground-contact | inverted Position Z | 2 / -.1 | ground_mud / .8 / .38 / .95 |
| Scratches | surface-scuffs | Edge Wear | 2 / .15 | scratches / .9 / .2 / .85 |
| Leaks & Stains | gravity-streaks | Cavity Dirt, gravity response | 2.8 / -.05 | leaks_streaks / .92 / .42 / .88 |
| Surface Dust | settled-dust | Light | 1.2 / .02 | fine_dust / .55 / .24 / 1.15 |
| Position height | height-fade | Position Z | 1 / 0 | none |
| Curvature edges | curvature-ridge | Curvature | 4 / -.5 | none |

Detailed generators default to response 1, optional inputs, bilinear maps,
triplanar grunge, surface fallback, AO/Curvature influence .8 and the other three
influences .15. Rust, Edge Wear and Leaks add a .25 Multiply Position gradient
(Z, Z and Y respectively; Rust inverted). Ground Dirt and Surface Dust add .25
Linear Dodge inverted AO. Ground Dirt also multiplies .25 upward Normal exposure.
Surface Dust uses light `(0,.35,.93675)` and smoothstep. Ground Dirt's primary
height uses smoothstep/invert. Crease Dirt uses Thickness influence .65 and
Curvature influence 1. Leaks uses response 2, Y axis, streak pattern 3, scale 2,
amount .3, AO influence .6, Curvature influence 1. The full exported JSON remains
the authoritative parameter record.

## Interchange and fidelity boundaries

`bts-blender-presets export` writes `preset.json`, the original native archive
(base64), decoded map/texture float payloads, and native reference evaluations.
Blender builds actual connected math/image/group trees and packs the payloads.
A Blender Text datablock retains the source manifest/archive. Green **Value**
nodes bind to typed JSON fields. Export reads their current values; unchanged
controls keep the original JSON precision. Native import parses with production
validators, retains original sparse paint/textures, writes with production
codecs, reopens for validation and atomically replaces the output.

Editable mappings include common map-generator arithmetic, per-effect opacity,
axis/curve/invert, light vector and used influences, native Levels, supported
scalar blend modes, uniform fills, unrotated triplanar bitmap fills with the same
12 sampling taps and normal-weight threshold, and a root-folder material made
of Normal-blended uniform fills. Material color, roughness and metallic controls
are exported. Match-per-UV-tile/nearest baked textures also import as image nodes.

**Not a general inverse shader compiler:** node rewiring, unknown operations,
changed source pixels, and unbound defaults fail a structural/pixel signature
check on editable export. Layout/labels may change. Ordinary graph duplication
or group renaming can also invalidate the signature. Use the explicit bake path
for freely authored graphs. This is deliberate failure, not an export that
silently ignores the graph.

Specific non-1:1 cases:

- Leaks & Stains' custom integer-hash noise/gravity generator is a labeled native
  reference image at the exported JL01 UVs/resolution. Its internal generator
  settings are locked; its opacity and surrounding height/fill/Levels remain
  editable. Stock Blender Noise is not the same function. Native Grunge,
  nonzero grunge amounts and curvature-neighborhood dilation also use explicit
  per-generator reference snapshots. The original procedural settings survive
  editable export, but those snapshots do not retarget themselves to a new mesh.
- Sparse paint, groups, filters, compare/anchor dependencies, other projection
  modes or material hierarchies outside the editable subset are represented by
  a clearly labeled **whole-preset reference snapshot**. Their original archives
  can roundtrip unchanged. Reference evaluations are for JL01, tile 1001, and
  cannot recreate external document anchors that are not in the preset.
- A shader-tree material preview exposes Base Color, Roughness and Metallic;
  native normal/bump/AO/custom/displacement channel state remains in the archive,
  not a claim of equivalent Blender BSDF rendering. The example uses only those
  three PBR outputs. Arbitrary Principled bake export also intentionally covers
  those three inputs, not transmission, displacement, lighting or a whole BSDF.
- UV texel sampling, float arithmetic and material lighting are separate issues.
  Validation compares linear scalar/PBR fields. The sphere sheet uses a shared
  native display transform; separate stock Blender mesh renders are also saved.
  Neither proves equivalent lighting between the two applications.
- Explicit bake export produces UV image fills: it loses source graph
  editability/geometry retargeting, preserves appearance on the baked UVs, and
  rejects out-of-range mask/roughness/metallic data. Keep the source `.blend`.
  Single tile 1001, 32–4096 pixel bake sizes; this is not an arbitrary UDIM baker.

## Commands

Inside `distrobox enter bts-build`, with paths for this checkout:

```bash
cd /mnt/mpainter-phase1
cmake --build build/linux-native --parallel 2 --target bts-blender-presets
# Supply these variables only to native Qt commands, not stock Blender:
xvfb-run -a env QT_QPA_PLATFORM=xcb \
  QT_PLUGIN_PATH="$PWD/toolchains/qt/plugins" \
  LD_LIBRARY_PATH="$PWD/toolchains/qt/lib:$PWD/build/runtime/lib" \
  build/linux-native/bin/bts-blender-presets export input.btsmask exchange

xvfb-run -a blender -b --factory-startup -t 2 --python-exit-code 1 \
  --python mpainter/scripts/blender/mpainter_presets.py -- import exchange/preset.json edited.blend
# Edit green Value nodes, save locally, then:
xvfb-run -a blender -b edited.blend -t 2 --python-exit-code 1 \
  --python mpainter/scripts/blender/mpainter_presets.py -- export exported
# With the native Qt environment above:
# bts-blender-presets import exported/<slug>.json edited.btsmask
```

The same export/import commands accept `.btsmaterial`. Import the resulting native
archive through the existing shelf resource importer. The bridge does not write
to the user's shelf automatically.

Freely authored scalar graph: connect its [0,1] result to an Emission shader and
Material Output. Bake on the desired mesh with its intended UVs:

```bash
xvfb-run -a blender -b authored.blend -t 2 --python-exit-code 1 \
  --python mpainter/scripts/blender/preset_bake.py -- \
  --material "My Mask" --object Shader_Ball_JL_01 --kind mask --output baked
# With native Qt environment:
# bts-blender-presets import-bake baked/bake.json authored.btsmask
```

Use `--kind material` with a directly connected Principled BSDF to bake its Base
Color/Roughness/Metallic inputs, then `import-bake ... authored.btsmaterial`.
No driver, Python handler, startup script or auto-executing code is saved in the
base file. Shader Ball and its derived maps retain the CC BY 4.0 attribution in
`assets/meshes/Shader_Ball_JL_01-NOTICE.md`; the `.blend` includes that notice.

## Reproduce the proof

`scripts/host/test-blender-presets-headless.sh` runs entirely inside bts-build and
wraps each application invocation in `xvfb-run`. Builds default to two jobs and
reject `BTS_JOBS` outside 1–6. Blender is `-b -t 2`, Cycles CPU. Stages and tests
run sequentially. No push, publication or upload step exists. The host-side
comparison script needs NumPy/Pillow; Blender has its own bundled NumPy. Set
`BTS_PYTHON` / `PYTHONPATH` for an existing local validation environment.

The proof cold-opens the saved base file, evaluates all ten groups with Cycles,
exports unchanged and edited trees, rejects unsupported graph edits, imports
archives through native codecs, compares pixels after native reevaluation,
exercises arbitrary scalar/Principled graph bakes, checks atomic rejection, and
writes side-by-side images and machine-readable measurements. See the delivery
report for actual results and remaining tolerances.
