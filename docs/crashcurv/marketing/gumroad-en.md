# mPainter 0.9.0 alpha

A 3D texture painter built on Blender, with a layer stack and node based material import. Build supported materials in Blender and bring them into mPainter as Material Presets. Some nodes and shading features have limits; import warnings show what needs attention.

**This is an alpha.** It works and I use it myself, but expect bugs, rough edges and changes between versions. Save often. The reported mesh-map rebake crash is fixed in the tested local build. A new AppImage with that fix has not yet been packaged. Linux is the tested platform today. Windows has no working release yet. macOS needs a viewport port.

**Buy the alpha now and you get every update up to and including 1.0 for free.**

## What's in 0.9.0 alpha
- Turquoise Dark interface with a layer stack, Texture Sets tab and Assets panel
- HDR studio lighting by default, rotate the light with Shift + right drag
- Texture Sets: uncheck sets or UV tiles to hide them and unload their resources to a lossless disk cache. Check them to reload. Shared resources can stay loaded; total RAM and VRAM usage may not fall
- Mesh map baking: curvature, ambient occlusion and thickness on evaluated subdivision and height/displacement. Curvature uses the Blender Cycles Pointiness algorithm on that geometry, independent of normal maps. Its detail follows mesh subdivision; it does not invent surface detail on a coarse mesh. AO and thickness have denoising enabled by default
- 4K and 8K mesh map paths passed earlier focused tests. Memory use depends on the mesh, maps and layers; these are not whole-project stress guarantees
- GPU assisted AO and thickness baking with CPU fallback. Cycles rendering uses OptiX, then CUDA, then CPU when no compatible GPU is available
- 10 Mask Generators: Crease Dirt, Curvature edges, Edge Wear, Ground Dirt, Leaks & Stains, Moss, Position height, Rust, Scratches and Surface Dust
- 47 built-in materials: 30 based on CC0 textures from Poly Haven and 17 original procedural ones, with UV and Triplanar mapping. Bundled texture maps are 1K
- Supported Blender materials as Material Presets, plus save a layer folder as a preset from the right click menu
- Shader_Ball_JL_01 as a built-in preview and project mesh
- Bitmap brushes, normal map import and 8K float height EXR import

## Price
- Alpha: SUGGESTION $29 (free updates to 1.0)
- 1.0 regular: SUGGESTION $59

## System requirements
- **Linux (tested):** x86_64, a compatible glibc based distro, X11 or XWayland and an OpenGL 4.3 GPU. The local AppImage was built on Fedora 43 and tested on Bazzite. Older distros are not verified. The AppImage does not bundle glibc or your graphics driver
- **GPU:** NVIDIA RTX 3090 is tested. Cycles can use OptiX or CUDA with a compatible NVIDIA driver. GPU support varies by backend
- **Memory and disk:** no validated minimum RAM or VRAM specification yet. 8K maps can use substantial memory. Allow space for the roughly 660 MiB AppImage, about 2.1 GiB when extracted, and additional project, cache and temporary files
- **Windows:** experimental build work only. No working application package or verified system requirements yet
- **macOS:** no working build. The current viewport requires OpenGL 4.3, which macOS does not provide. A Metal viewport integration is required; it is not implemented

## FAQ
**Is it stable?** No, it is an alpha. It is usable for real work but save often and keep backups.
**Do I get 1.0?** Yes, alpha buyers get all updates to 1.0 for free.
**Can I use my Blender materials?** Supported node materials can be imported as Material Presets. Not every node or shading feature is reproduced. Standard material import does not recreate geometric displacement from the source graph.
**Does it open files from other texturing apps?** It imports normal and height maps. It does not open or render .sbsar archives. The separate metadata tool only reads already extracted description XML.
**Which GPU do I need?** The viewport requires OpenGL 4.3. The tested NVIDIA RTX 3090 supports Cycles OptiX and CUDA. Mesh map baking and Cycles have CPU fallback paths; the viewport still needs a compatible GPU.
**Refunds?** (Mattias decides.)

## License and credits
- mPainter is based on Blender and released under **GNU GPL v3 or later**. You are free to use, modify and share it under the GPL. What you pay for is the ready built program, updates and supporting the work. The matching source package for this alpha still needs to be prepared before release
- Built-in materials: textures from [Poly Haven](https://polyhaven.com), CC0, plus original CC0 procedural materials
- Default Studio HDR: made in Blender by Mattias Lind. Optional Blender studio HDRIs: CC0, Greg Zaal / Poly Haven
- [Shader Ball JL 01](https://sketchfab.com/3d-models/shader-ball-jl-01-9ecc1d8ddb5b4059a80d0bd925e3dd3b) by JLRIN / Craneo (CRANEO_), [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/). Converted to an upright OBJ and used for derived preview maps
- Mask grunge inputs derived from ambientCG CC0 scans
- Blender is a trademark of the Blender Foundation. mPainter is not affiliated with or endorsed by the Blender Foundation
