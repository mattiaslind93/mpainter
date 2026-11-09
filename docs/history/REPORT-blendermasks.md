# Blender Smart Masks delivery — 2026-11-05

Implemented and verified locally on `main`. Nothing was pushed, published or
uploaded. Builds used **`--parallel 2`**; tests/renders ran sequentially. Stock
Blender ran in background (`-b -t 2`), with application validation under
`xvfb-run` inside `distrobox bts-build`. No visible application windows were opened.

## Deliverables

- Base file: [Shader_Ball_JL_01-mPainter.blend](/home/mlind/mpainter-work/mpainter/assets/blender/Shader_Ball_JL_01-mPainter.blend).
- Full architecture/format/input/parameter/storage mapping and usage:
  [docs/blender/masks.md](/home/mlind/mpainter-work/mpainter/docs/blender/masks.md).
- Native bridge: `build/linux-native/bin/bts-blender-presets`; source
  `native/host/BlenderPresetBridge.cc`, registered as a CMake target.
- Blender importer/exporter: `scripts/blender/mpainter_presets.py`.
- Explicit arbitrary-graph bake exporter: `scripts/blender/preset_bake.py`.
- Reproduction: `scripts/host/test-blender-presets-headless.sh`.
- All images and JSON evidence: [shots/blendermasks/](/home/mlind/mpainter-work/shots/blendermasks).
- Native archives, exchange payloads, edited examples and logs:
  `/home/mlind/mpainter-work/build/blendermasks/`.

The `.blend` SHA-256 is
`4ce4ebef0294a62cd525c301ff2aaabaf69b7de98de65d7295954421da6730ef`.
It is a tracked local asset, not a reference to an external download.

## Architecture findings

mPainter has **ten named shelf masks**, composed from **nine active low-level
generator kernels** plus the None entry. Eight shelf recipes combine mesh-map
generators, optional additional height/AO/normal effects, a triplanar bitmap
Multiply, and Levels. Position height and Curvature edges are single-generator
recipes. The implementation exports the actual compiled built-in recipes.

Mask metadata is ordered native effect JSON with UUIDs, scalar blend/opacity,
map-generator settings, levels and bitmap projection settings. `.btsmask` is a
version-6 qCompress archive with sparse scalar paint and embedded float textures.
Material Presets are captured layer-folder subtrees, serialized in a version-1
`BTSHIST2` archive with the schema-79 native document, sparse channel/mask paint,
embedded textures and internal anchor references. Captured presets omit mesh maps
and runtime geometry: destination maps drive their generators. The user shelf is
`AppDataLocation/shelf`, `library.json` plus content-addressed `blobs/` (overridable
with `--library-dir`). See the architecture document for exact source files,
roles, parameters, recipe table, equations and storage boundaries.

## Base file and compatibility

The file contains the original JL01 mesh, **38,651 vertices**, and **111,876 UV
loop entries**. A separate background Blender process compared every loop UV
against the original OBJ's float32 coordinates: exact equality, no unwrap,
repacking or axis conversion.

It includes packed Non-Color Image Texture inputs and groups for Curvature,
AO, Thickness, Position, World Normal and ID using `shaderball-jl01-map-N.png`
names. The five existing 512² maps are resampled through mPainter's own preview
surface code to the 256² reference grid. The original OBJ has one material ID;
the new red ID map is rasterized from that assignment, with coverage alpha. It
is not presented as a recovered pre-existing ID bake. The source attribution is
included both beside the asset and inside its `.blend` Text datablocks.

All ten named masks are Blender groups. Common generators, Levels, supported
blend operations and triplanar bitmap fills are actual connected shader nodes.
Green Value nodes edit parameters that the exporter reads from the live graph.
The file also contains a layered metal/rust Material Preset with editable color,
roughness, metallic and mask parameters. Native import uses the production
codecs, preserves original sparse payloads and textures, validates the result,
and writes atomically.

**Compatibility is scoped, not a general inverse shader compiler.** The custom
integer-hash gravity/noise stage in Leaks & Stains is a labeled baked reference;
its surrounding height/fill/Levels remain editable. Other unsupported native
operations use labeled whole-preset snapshots while retaining their archives.
These reference snapshots are fixed to JL01/256². Structural graph edits outside
the supported parameter controls are rejected. Freely authored Blender graphs
have an explicit lossy UV bake route to native mask/Material Preset image fills.
The Principled bake route covers Base Color, Roughness and Metallic; it is not a
full BSDF/displacement/UDIM converter. These limitations are documented in detail.

## Roundtrip evidence

Stock **Blender 5.0.1**, Cycles CPU, 256×256 linear field comparisons:

| Preset | Channel | Mean absolute error | Maximum absolute error |
|---|---|---:|---:|
| Crease Dirt | Mask | 6.87e-08 | 1.16e-05 |
| Curvature edges | Mask | 0 | 0 |
| Edge Wear | Mask | 1.89e-08 | 5.69e-06 |
| Leaks & Stains | Mask | 2.66e-08 | 1.11e-05 |
| Ground Dirt | Mask | 1.63e-07 | 5.3e-06 |
| Position height | Mask | 0 | 0 |
| Moss | Mask | 2.34e-07 | 7.66e-06 |
| Rust | Mask | 1.27e-07 | 5.25e-06 |
| Rust Material | Base Color | 3.47e-08 | 2.26e-06 |
| Rust Material | Roughness | 6.95e-08 | 2.8e-06 |
| Rust Material | Metallic | 1.03e-07 | 4.2e-06 |
| Surface Dust | Mask | 2.19e-07 | 2.34e-05 |
| Scratches | Mask | 1.07e-08 | 9.58e-06 |

Acceptance bounds are mean `< 1e-6`, maximum `< 1e-4`; all pass. Maximum measured
error across the default and edited mask comparisons is **2.3365021e-5** on the
0–1 scale. The two simple masks are numerically exact. The metrics include the
explicitly declared Leaks reference stage; they do not prove a procedural
translation of that stage.

- **10/10 unchanged `.btsmask` archives returned byte-for-byte identical.**
- The layered `.btsmaterial` retained its layer metadata and native evaluated
  pixels exactly. Its container bytes differ because native serialization creates
  fresh standalone document/set identities; byte identity is not claimed for it.
- **11/11 parameter edits** (ten mask controls and material roughness) produced
  observable native output changes and matched the corresponding edited Cycles
  evaluations within the same bounds.
- **11/11 altered node-operation graphs were rejected**, rather than exporting
  stale retained settings. **11/11 changed image projections were also rejected.**
  Invalid bridge versions were rejected atomically in
  all eleven cases, leaving sentinel output files intact.
- A Blender-authored UV/sine scalar graph and procedural Principled graph were
  baked, imported as actual `.btsmask` / `.btsmaterial`, reevaluated natively,
  re-exported and cold-imported into Blender image nodes. All four scalar/color
  fields were **exact** through that bake roundtrip.
- A real nested native Group mask with embedded child textures used the labeled
  whole-preset snapshot fallback and returned **byte-for-byte identical** after
  Blender export and native import.
- Existing `bts-mask-generator-tests`, `bts-mask-preset-tests` and
  `bts-smart-material-preset-tests` passed sequentially.
- Python syntax checks, shell syntax and `git diff --check` passed.

The native side is the production C++ CPU evaluator and archive codecs, not a
Python reimplementation. This delivery does not claim a new end-to-end mPainter
GPU/GUI shelf automation test; the existing native codec tests also exercise
library reload, preset application, anchors and corruption rejection.

## Visual comparison

- [Real JL01 mesh comparison sheet](/home/mlind/mpainter-work/shots/blendermasks/comparison-mesh.png):
  mPainter-evaluated fields versus Blender nodes, both rendered through the same
  stock Cycles camera/light. Ten masks plus the layered material; differences ×10.
- [Native sphere-display sheet](/home/mlind/mpainter-work/shots/blendermasks/comparison-sphere.png):
  native mask values versus actual Blender node-bake values through the same
  mPainter preview display. These are not screenshots of a visible UI.
- [UV field comparison sheet](/home/mlind/mpainter-work/shots/blendermasks/comparison-unchanged.png).
- Individual edited, arbitrary-graph and real-mesh pairs are in the same folder.

Real-mesh renders use 8 mask samples / 64 material samples, CPU with two threads.
The maximum display-space difference is **one 8-bit code value**, with a maximum
mean difference of **0.00004578 code values**. Five pairs are pixel-identical.
Both sides use the same lighting; this isolates mask/PBR field compatibility and
does not claim matching application render engines. The sheets were visually
inspected: no flipped UVs, shifted islands or visible residual differences.

Machine-readable evidence:
`shots/blendermasks/results.json` and `mesh-render-results.json`.
The temporary environment for the comparison scripts uses NumPy 2.5.4/Pillow
13.0.0 in `build/blendermasks/python`; production dependencies were not changed.

## Reproduce locally

```bash
distrobox enter bts-build -- env \
  PYTHONPATH=/mnt/mpainter-phase1/build/blendermasks/python \
  bash /mnt/mpainter-phase1/mpainter/scripts/host/test-blender-presets-headless.sh
```

The runner separates stock Blender's runtime from mPainter's Qt/USD library
paths and strips the host Python path for Blender. It rejects build job counts
above six and defaults to two. Re-run output stays local. The source docs include
individual export/import and arbitrary-graph bake commands.

Commit: see the local `main` commit for this report; the external delivery copy
records the final commit hash below.
