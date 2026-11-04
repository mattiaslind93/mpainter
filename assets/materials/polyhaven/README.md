# CC0 starter materials

47 editable native Blender Material Presets: 30 distinct Poly Haven textures and
17 original procedural shaders. They appear in **Assets > Materials** and
**Material presets**, with Built-in scope. Select a material, then use the usual
Fill **UV / Tri-planar** controls; the native graph and original source maps stay
intact. The same native import/projection pipeline is used for user libraries.

Texture originals are 1K (156 files, 246,261,776 bytes). The initial selection at 2K
exceeded the requested 400 MB budget.
Diffuse, roughness, metallic and AO use original JPEG files where available;
OpenGL normals and displacement use original PNG files. No image is recompressed.
`manifest.json` records the final neutral names, reviewed substitutions, URLs,
resolution, original MD5 and independently computed SHA-256. Every map is retained
as a disk-backed dependency; browsing the shelf does not decode the whole library.

`native/*.btsnative.blend` are mPainter native material archives, containing the
editable Blender source graph, image dependency manifest and a 256px preview.
`native/*.json` records the imported interface and channel support. The source
normal/Bump graph uses the current mesh; source displacement is retained separately
and follows the application's native Height policy. AO is multiplied into base
color. Missing metallic maps use dielectric defaults, except bare galvanized
steel, which uses metallic 1. Surface names are approximations, not species or
physical measurement certifications.

## License and credits

Textures: [Poly Haven](https://polyhaven.com), [CC0 1.0](https://polyhaven.com/license).
Procedural shaders: original mPainter work, CC0 1.0.
Thumbnails: independently rendered with Blender Cycles CPU, 24 samples, four
threads, on **Shader_Ball_JL_01**. The preview mesh is by JLRIN / Craneo, CC BY 4.0;
see `../../meshes/Shader_Ball_JL_01-NOTICE.md` and the root `NOTICE.md`.
No Poly Haven website example renders are redistributed.

## Reproduction

From the repository, first download the exact reviewed originals:

```sh
python3 scripts/polyhaven/download.py \
  --mapping assets/materials/polyhaven/manifest.json \
  --work ../build/polyhaven/api --output ../build/polyhaven/textures
```

Build `bts-host` with at most `cmake --build … --parallel 6`. The local helper uses
`/mnt/mpainter-phase1` inside `bts-build` and the existing Blender 5.2 binary:

```sh
distrobox enter bts-build -- bash /mnt/mpainter-phase1/mpainter/scripts/polyhaven/run-headless.sh generate
distrobox enter bts-build -- bash /mnt/mpainter-phase1/mpainter/scripts/polyhaven/run-headless.sh test
python3 scripts/polyhaven/verify.py --library assets/materials/polyhaven \
  --ui ../build/polyhaven/ui --output ../build/polyhaven/verified.json
```

Generation is resumable; it skips existing preview/import pairs. For regeneration,
remove only the selected generated files from a disposable checkout/output copy.
Each render and import runs in its own process, serially, under Xvfb. `test` uses
a single Cherry Wood material and the unchanged New Project default of 1024px;
`test-mapping` uses four materials at 128px to bound retained history memory.
Source images always retain their bundled dimensions. The status bar reports the
Texture Set composite size, not the source image size. Existing 128px projects
keep their authored size; choose 1024 in Texture Set settings to restore detail. Never run this helper alongside another heavy GUI/GPU test or
AppImage build. `scripts/package-linux.py` and CMake installation both include this
library; `scripts/package-appimage.py` compresses with two workers.


## Material correction (2026-11-04)

Wool now uses `polar_fleece`, an unpatterned wool-like napped fleece approximation.
The former `wool_boucle` maps were valid originals but depicted a yellow/black
checker weave (also documented in the API tags); this was not a missing-file
fallback. Raw Iron, Rough Iron and Rough Steel use distinct original procedural
mill scale, granular cast surface and directional abrasion. Brushed Iron uses
finer directional abrasion; Grainy Plastic is sage green; Walnut Wood has an oiled tint. Calf Hide is warm
tan and Worn Leather is faded cool grey-brown.
Original source images remain unchanged and editable in the native graph.

To regenerate just the corrected previews and archives after downloading:

```sh
distrobox enter bts-build -- env \
  BTS_POLYHAVEN_SLUGS='wool raw-iron rough-iron rough-steel brushed-iron grainy-plastic walnut-wood calf-hide worn-leather' \
  bash /mnt/mpainter-phase1/mpainter/scripts/polyhaven/run-headless.sh regenerate
python3 scripts/polyhaven/contact-sheet.py --library assets/materials/polyhaven \
  --output ../matfix-contact-sheet.jpg
```

Use `BTS_POLYHAVEN_DETAIL='Wool'` with `test` for a separate one-material 1K proof.
After replacing texture sources, remove obsolete `native/images` files only once
no manifest/metadata references them; `verify.py` rejects orphan images.

`test-resident` runs the focused 128px native geometry regression (actual GPU index
count, rendered pixels, cache reuse, Undo, and graphics state). The triangle
upload API takes a primitive count, not the number of indices; this regression
protects the fixed out-of-bounds read during native material application/reopen.
