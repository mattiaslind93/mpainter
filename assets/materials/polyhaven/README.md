# CC0 starter materials

47 editable native Blender Material Presets: 30 distinct Poly Haven textures and
17 original procedural shaders. They appear in **Assets > Materials** and
**Material presets**, with Built-in scope. Select a material, then use the usual
Fill **UV / Tri-planar** controls; the native graph and original source maps stay
intact. The same native import/projection pipeline is used for user libraries.

Texture originals are 1K (157 files, 251,393,682 bytes). The same selection at 2K
would require 988,796,073 bytes, exceeding the requested 400 MB budget.
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
Each render and import runs in its own process, serially, under Xvfb; project tests
use 128px textures. Never run this helper alongside another heavy GUI/GPU test or
AppImage build. `scripts/package-linux.py` and CMake installation both include this
library; `scripts/package-appimage.py` compresses with two workers.
