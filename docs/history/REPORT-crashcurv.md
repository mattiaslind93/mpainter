Rebake crash and geometry Pointiness, 5 November 2026.

The local build fixes the bake mesh's invalid custom-normal fan lookup. Curvature now uses the Cycles Pointiness algorithm on evaluated geometry after subdivision and active height/displacement. All work stayed local on `main`. Builds used `--parallel 2`; graphics tests used private `xvfb-run` sessions inside `bts-build`. Blender ran with `-b --threads 6`. Builds and heavy tests ran sequentially.

The original exit-139 trace is retained at `../marketing/evidence/mesh-rebake-crash.txt`. The preserved stripped executable was found under `../build/tmp/appimage_extracted_8c39f48cc8095f858c5b8e279146bf94/`. Matching 64 unchanged instruction bytes against the symbol-bearing build identified PC `0x1b61501` as `blender::bke::mesh::mesh_normals_corner_custom_set`, and `0x1b61fde` as its `mesh_set_custom_normals` caller. A further 32-byte match identified the MeshBakeJob worker. The failure is in CPU surface preparation, before the first rebake map or GPU ray dispatch.

Coincident corners can acquire the same vertex ID when the surface is welded. Blender's legacy compact custom-normal encoder assumes every corner belongs to a normal fan. Its corner collector finds the first occurrence of a vertex within a face; a repeated corner can remain without an initialized fan index. The first custom-normal loop indexes `corners_by_space[space_index]` without validating that index. This is an invalid mesh-normal lookup, rather than evidence of an old 64² texture buffer being reused at 256². Heap reuse explains why rebaking can expose it, though the historical trace alone cannot recover the original bad index or heap contents.

A controlled collapsed-face fixture reproduced the mechanism. Without instrumentation it could pass or produce different output on returning to 256². A temporary bounds diagnostic in Blender, with its memory-debug allocator enabled, produced:

```text
BTS_REBAKE_BEGIN size=64
BTS_DIAG_INVALID_NORMAL_FAN corner=4 space=-1 spaces=99 corners=99
```

That diagnostic intentionally aborted with exit 134; it is not a claim that the historical exit-139 UI session was replayed exactly. The temporary diagnostic was removed and Blender's build source restored byte for byte. The original fault site and the controlled invalid index establish the faulty mechanism. See [diagnostic output](docs/crashcurv/crashcurv-diagnostic.log).

`TexturePadding.cc` now stores normalized Float3 `custom_normal` attributes directly on mesh corners. This is Blender's supported free-normal representation and does not need a fan index. Missing or zero normals retain a geometric fallback. Both the project rasterization mesh and the external high-source mesh use this path. Supplied normals remain floats instead of being converted to compact fan coordinates. No GPU cache flush workaround or resolution cap was added.

The new regression runs **64 → 256 → 1024 → 256 → 64 → 1024 → 64** with all seven map types. The CPU fixture reuses the same projection-geometry cache, retains prior maps during subsequent jobs, and checks actual plane dimensions, finite pixels, occupied coverage, neutral plane curvature/visibility, supplied shading normals, and identical hashes at repeated sizes. Clean and collapsed fixtures passed all 98 maps. The collapsed fixture uses Blender's memory-debug allocator. The fixed run includes:

```text
BTS_REBAKE_PASS size=64 maps=7
BTS_REBAKE_PASS size=256 maps=7
BTS_REBAKE_PASS size=1024 maps=7
BTS_REBAKE_PASS size=256 maps=7
BTS_REBAKE_PASS size=64 maps=7
BTS_REBAKE_PASS size=1024 maps=7
BTS_REBAKE_PASS size=64 maps=7
```

The workspace dialog repeated that sequence on the bundled shaderball at 128² document resolution, with four secondary rays and default OIDN. All 49 maps completed; all 14 AO/thickness tasks used the GPU. Repeat-size hashes, undo/redo, and save/reopen hashes matched exactly. The application exited cleanly. CPU Pointiness/geometry tests and bake preference tests passed. The production surface test also checked normal-map independence, signed negative displacement and source immutability. Existing two-UDIM OBJ/Alembic/USD, frame/unit and cage tests passed after changing external-source normal storage; maximum cross-format map error was below 0.000001. See [validation](docs/crashcurv/validation.json) and [workspace captures](docs/crashcurv/workspace.json).

At baseline `fca2b7f`, curvature estimated changes in smooth vertex normals over a world-space radius, blurred the estimate, and applied a tanh mapping. By default those normals were geometric; optional normal-map shading could also contribute. It already consumed evaluated subdivision and active displacement, but it was not the Pointiness calculation.

Now each welded vertex averages the directions of its actual incident edges, measures the angle against the geometric surface normal, divides by pi, then averages the scalar with adjacent vertices once. This follows Cycles `attr_create_pointiness` in the pinned Blender source. Original polygon triangulation diagonals are excluded from adjacency. Newly generated subdivision triangles carry their actual edge metadata. Coincident UV copies within an object share the geometric field, which is interpolated across the surface. Normal maps do not contribute, including when a legacy project stored a nonzero normal-detail setting. The UI's optional additional smoothing is separate: radius 0 and strength 1 preserve the native Pointiness values; old saved strength/radius values remain explicit adjustments.

The headless comparison used 256² float maps and Blender Cycles Geometry Pointiness connected to Emission on the exact exported evaluated/displaced triangles. Before and after geometry exports have identical SHA-256 checksums. The first fixture is a Blender-subdivided sphere with additional triangle subdivision and signed height displacement. The shaderball is a second control at its original tessellation. Raw float errors over common covered texels, including margins:

| Mesh | MAE before | MAE after | Maximum after |
| --- | ---: | ---: | ---: |
| Subdivided, displaced sphere | 0.00836413 | 0.00002492 | 0.00187629 |
| Shaderball control | 0.06451401 | 0.00421023 | 0.53276241 |

[Before / after / Blender comparison](docs/crashcurv/crashcurv.png). The delivered sibling file is `../crashcurv.png`. These are actual background renders of measured bake outputs, with identical camera/material and fixed 3× display gain around 0.5. Raw maps are not normalized per image. Artifacts and complete logs remain in `../build/crashcurv/` and `../logs/crashcurv-*.log`.

This is not a bit-exact parity claim. The shaderball has localized atlas-boundary differences: eroding the common coverage mask by four texels reduces MAE to 0.001408 and maximum error to 0.09546. Those exclusions are not used in the headline table. Blender and mPainter also have different positional welding tolerances. Nonplanar quad-normal parity is not covered by these triangulated reference fixtures. Like Blender Pointiness, the result depends on surface tessellation; it cannot make a coarse source smooth without subdivision. The displaced sphere comparison and welded-vertex/quad-diagonal tests check the requested continuous geometry behavior. The secondary shaderball's remaining polygon structure is also visible in Blender's reference.

The six marketing documents in `../marketing/` were updated. Curvature is described as geometric Pointiness, AO/thickness as GPU-assisted with CPU fallback, and OptiX as a Cycles rendering backend rather than the mesh baker. Claims do not promise exact equality or automatic detail on coarse meshes. The earlier AppImage has not been repackaged; the drafts distinguish it from the tested local native build at `../build/linux-native/bin/mpainter`. No new 4K/8K validation is claimed. Verbatim copies of the six edited drafts are committed under `docs/crashcurv/marketing/`; their relative links are intended for the canonical sibling marketing directory. Original capture evidence and its historical hashes were preserved.

Re-run inside `distrobox enter bts-build`, with no other heavy workload running:

```bash
cd /mnt/mpainter-phase1/mpainter
cmake --build ../build/linux-native --parallel 2 --target mpainter bts-surface-curvature-tests bts-bake-preferences-tests
BTS_REBAKE_DEGENERATE=1 scripts/host/test-rebake-headless.sh ../build/crashcurv/rebake-degenerate
scripts/host/test-rebake-headless.sh ../build/crashcurv/rebake-clean
scripts/host/test-rebake-workspace-headless.sh
BTS_SURFACE_FIXTURES=/mnt/mpainter-phase1/build/crashcurv/fixtures BTS_SURFACE_EXPORT=1 BTS_SURFACE_REGRESSION=1 scripts/host/test-surface-bakes-headless.sh /mnt/mpainter-phase1/build/crashcurv/after
xvfb-run -a /home/mlind/bts/build/host/bin/blender -b --factory-startup --threads 6 --python scripts/host/compare-surface-bakes.py -- reference /mnt/mpainter-phase1/build/crashcurv/fixtures
```

`scripts/host/verify-rebake-workspace.py` checks the saved workspace captures and is invoked by its launcher. `verify-pointiness-parity.py` measures the raw reference errors and rejects regression to the former estimate. `render-surface-bake-comparison.py` and `assemble-surface-bake-comparison.py` regenerate the image. Pstack's Fix Root Causes principle led to tracing and checking the fan index; Model the Domain led to storing explicit per-corner normals rather than carrying invalid fan state.
