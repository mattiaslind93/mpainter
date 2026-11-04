# Notices

Copyright Mattias Lind. Original mPainter code and artwork are distributed under GPL-3.0-or-later; see LICENSE. Existing third-party copyright and SPDX headers remain applicable.

- **Blender 5.2.0**: Blender Authors, GPL-3.0-or-later for the combined distribution; individual files retain their upstream GPL-2.0-or-later or other notices. The integration patch is in `third_party/blender/patches`. Blender is fetched from its official source repository; it is not vendored here.
- **Qt 6.8.3**: The Qt Company and contributors, LGPL-3.0/GPL-3.0 and applicable permissive component licenses. Qt is dynamically linked and may be replaced by a compatible build. Packaging normalizes Qt assertion-source path strings to relative names without changing ELF layout or functionality; the postprocessing is in `scripts/package-linux.py`. Qt sources: https://download.qt.io/archive/qt/6.8/6.8.3/submodules/qtbase-everywhere-src-6.8.3.tar.xz . Preserve Qt's complete license texts in binary distributions.
- **File browser icons**: Blender Authors, GPL-2.0-or-later. Original SVGs, atlas, manifest and license are in `assets/file-browser`. These are generic Blender file type icons.
- **Splash**: artwork supplied by Mattias Lind for this release; the approved 852 × 531 PNG crop is preserved without further cropping. No independent provenance beyond the owner's supplied input is asserted.
- **Application icon**: Mattias Lind’s approved m mark; the supplied SVG, PNG, ICO and ICNS exports are preserved in `assets/branding`. About wordmarks use Inter Bold outlines/raster artwork; the SIL OFL notice is included in `assets/branding/licenses/Inter-OFL.txt`.
- **Sphere**: original mathematical UV sphere generated for this release. Mask thumbnails use the Shader_Ball_JL_01 baked preview surface restored from the mfPainter reference; provenance is recorded in `assets/mask-preview/README.md`. Test meshes in `native/tests/fixtures` are project-generated fixtures.
- **Mask grunge inputs**: eight grayscale maps derived from ambientCG CC0 surface scans, restored from the mfPainter reference. Sources and checksums are in `assets/grunges/README.md` and the packaged `licenses/mask-grunges.md`. Masks are evaluated by native code; no Adobe `.spmsk` archives are bundled.
- **Fonts**: no fonts bundled in this repository; the interface uses installed system fonts. Blender runtime fonts retain upstream notices. Its studio environment maps are CC0, credited to Greg Zaal / Poly Haven in the runtime studio-light license file.
- **AppImage runtime** (when packaged): probonopd and contributors, MIT; pinned type2 runtime 20251108, with its license included in the AppImage.
- **OpenPBR reference graph**: Academy Software Foundation, OpenPBR 1.1.1, Apache-2.0. The graph and license are in `native/host/openpbr`; the accompanying README records revision and hashes. This is a shader model implementation, not a bundled material preset.
- **Native libraries**: Assimp (BSD-3-Clause), HDF5 (BSD-style), OpenColorIO (BSD-3-Clause), OpenImageIO (Apache-2.0), OpenEXR/Imath (BSD-3-Clause), OpenSubdiv (Apache-2.0), OpenVDB (MPL-2.0), oneTBB (Apache-2.0), Embree (Apache-2.0), Python (PSF), zlib (Zlib), libpng (libpng), FreeType (FTL/GPL), and other Blender dependencies retain their respective upstream licenses. Blender's precompiled library `deps.md` records component versions and source URLs. Binary packages must retain the accompanying upstream copyright/license texts.

Poly Haven is an external source of CC0 materials: https://polyhaven.com/license . No materials from that service are bundled. Imported user content remains subject to the user's own rights and licenses.

When redistributing a binary, provide the matching mPainter source, Blender source and patch, build scripts, and corresponding dependency source/license information as required by their licenses. The local phase-one package is for review; do not interpret this notice as evidence that an unpublished source URL exists.

## Default Studio HDRI

New projects use `default-studio.hdr`, a Radiance environment made in Blender
by Mattias Lind and supplied by him for bundling in mPainter on 2026-11-04.
It illuminates the scene and reflections; the camera background uses the UI theme.
The asset and provenance notice are in `assets/environments/` and are checked by
both Linux packaging steps. SHA-256:
`48505ffc80c0ca026775c77621b5a85a4c44b0fb7819ddadfa9e7bc6f5732031`.

Blender's optional world HDRIs retain their adjacent CC0 notice and attribution
to Greg Zaal / Poly Haven. They are not the default environment.
