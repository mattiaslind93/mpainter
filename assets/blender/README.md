# mPainter Blender masks

`Shader_Ball_JL_01-mPainter.blend` was generated and cold-opened in stock Blender
5.0.1. It contains the original mesh/UVs, packed Non-Color mesh maps, input groups,
ten shelf-mask groups and a two-layer Rust Material Preset example. All assets
are local; no scripts execute when opening the file.

The green Value nodes are editable export controls. The custom Leaks & Stains
noise stage is explicitly labeled as a baked reference. The original archive and
procedural parameters remain packed in the file. Freely rewired graphs use an
explicit UV bake export rather than the parameter-preserving exporter.

See [formats, mappings, limitations and commands](../../docs/blender/masks.md).
Reproduce with `scripts/host/test-blender-presets-headless.sh` inside bts-build.

The mesh and derived maps are CC BY 4.0, JLRIN / Craneo; the complete
[notice](../meshes/Shader_Ball_JL_01-NOTICE.md) is also a Text datablock inside the
file. Grunge images retain the provenance in [assets/grunges](../grunges/README.md).
The five source UV map PNGs are resampled from 512² to the native 256² reference
grid; ID is rasterized from the OBJ's single material assignment. The comparison
scripts verify the source vertex count and every UV loop coordinate exactly.
