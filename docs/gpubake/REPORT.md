# GPU baking phase 1 · 2026-11-06

Implemented and verified locally on `main`. The original WIP in AutoBakeEngine, BakeQuality, MeshBakeDialog, SkewController and TexturePadding was reviewed and retained/extended. No push, publication, upload or packaging was performed.

AO and Thickness now trace on GPU with match-by-name object sets and per-object backface flags. A missing match entry is unrestricted; an explicit empty set rejects every object, matching CPU RayScene. Project-mesh name matching selects each object's own geometry. GPU failure keeps the original full-resolution CPU fallback and `BTS_BAKE_GPU_FALLBACK` logging.

Ignore Backface is an optional Common Parameters checkbox in both the Bake Mesh Maps dialog and Baking panel. Both use the existing `ignore_backface` template key. New defaults are OFF; explicit saved ON values survive. OFF leaves the object filter empty regardless of suffix. ON applies to all objects when the suffix is empty, or only source objects containing the configured marker. Source projection uses the same selection rule. Enabling it does not force CPU baking.

The Baking panel previously supplied no GPU callback. It now queues GPU work from its serial bake worker to the GUI/context owner. Cancellation and shutdown service pending requests without leaving a worker waiting on an abandoned callback. The modal dialog already had GPU ownership plumbing.

Pointiness uses compute passes over the existing CPU-welded polygon-edge graph. Welding, angle-weighted normals, polygon boundaries and connected components remain shared with SurfaceCurvature. The shader evaluates averaged unit-edge directions, the acos-equivalent atan2 angle, one neighbor blur and optional area-weighted radius smoothing. Radius smoothing visits the same balanced spatial tree in the same order with double accumulation. Full-resolution CPU fallback remains available. Curvature logs `BTS_BAKE_GPU_MAP uid=… tile=… role=4 batches=…`; its field is computed once and reused across tiles.

Draft selects 64 rays, sampling 1 / None, and denoise OFF. Final selects 256 rays and denoise ON, preserving the current subsampling choice. Presets are selectable in both UIs. Existing custom settings are not replaced just by opening a dialog. OIDN remains explicitly CPU-only, with its existing six-thread limit.

All builds used at most `--parallel 3`. Graphics/UI tests ran under private `xvfb-run` inside `distrobox bts-build`, never on :0. Builds and heavy tests were sequential. The new wrapper restricts tests/benchmarks to six logical CPUs and OpenMP to six threads. No user GUI process was stopped.

Numerical and GUI verification passed:

- Production 64² AO/Thickness maps at 256 rays, denoise OFF. Match filtering OFF/ON crossed with Ignore Backface OFF, suffix-only and all objects. All 12 map comparisons were bit-exact against CPU, maximum absolute RGBA error **0**, within the required **2e-5** tolerance. Tests include absent and explicitly empty match entries, and verify that enabled filters change the image. Injected GPU failure reproduced each CPU map exactly.
- MakeHuman Pointiness, 15,332 welded vertices and production 64² Curvature maps. Radius 0 had maximum absolute error **1.7881393e-7**; radius 0.351385236 (2% of scene extent) had **1.1920929e-7**. Both use a **2e-5** limit. Injected GPU failure was bit-exact against CPU.
- Real modal/panel checkboxes default OFF, preserve old explicit ON, round-trip through templates and project JSON, and expose working Draft/Final controls. Six real panel bakes (AO/Curvature/Thickness × OFF/ON) and four real modal ray-map bakes completed GPU work and published maps. The `BTS_BAKE_GPU_MAP` records are in `build/gpubake/phase-one.log`.
- Existing GPU ray reference, traversal continuation, simulated low-memory fallback and pending-request cancellation checks passed. The legacy batching fixture's simulated budget was reduced from 1 MiB to 512 KiB because 1 MiB fits all its samples under the current planner; the test now genuinely exercises multiple batches. Original GPU-reference samples remain bit-exact.
- Linux build, bake quality, BVH planner, name matching, surface curvature, displacement bake and bake preferences tests passed. Surface regressions also checked neutral planes, normal-map independence and signed displacement. New tests retain active assertions in Release builds.

The strict image test caught a one-ray shared-edge discrepancy during implementation. Explicit arithmetic order and a rounded reciprocal removed it from the production GPU ray path. The legacy diagnostic API keeps its original arithmetic. No image tolerance was loosened.

Blender Pointiness parity was checked using the existing background Cycles reference renders in `build/crashcurv/fixtures/`, with fresh CPU and GPU 256² bakes on the same fixtures. The two ray maps used 256 rays and CPU OIDN. All four AO/Thickness CPU/GPU image pairs were bit-exact, including denoise. The largest Curvature CPU/GPU difference was **2.3841858e-7**, below **2e-5**.

| Pointiness fixture | CPU → Blender MAE | GPU → Blender MAE | GPU → CPU maximum |
|---|---:|---:|---:|
| Displaced sphere | 0.0000249173819 | 0.0000249168379 | 0.0000001788 |
| Shaderball | 0.004210231681 | 0.004210231518 | 0.0000002384 |

The existing Blender acceptance thresholds remain 0.0001 and 0.005 MAE respectively. These are common covered-texel comparisons including margins, with no new exclusions. Existing localized shaderball atlas-boundary differences remain, including the unchanged maximum Blender error of 0.5327624. This preserves the previous CPU/Blender look; it is not a claim of bit-exact agreement with Blender itself. No new Blender render was needed because the retained references describe the unchanged source fixtures.

Character timings used `build/blender/tests/files/io_tests/obj/makehuman.obj`, SHA-256 `b83206ecf203bae6a6d05febfce53edc1345ee596efdf7ffa9e6095070a99d63`, with 28,632 triangles. The loader's absent companion MTL warning does not affect this geometry-only test. One material/tile was baked, at 2K and 4K. Every row used **64 rays, sampling 1, denoise OFF, dilation 0, match-by-name ON and Ignore Backface ON for all objects**. Hardware was NVIDIA RTX 3090 and Intel i7-6700K; affinity was logical CPUs 0–5. The container's existing memory limit was 6 GiB.

The “before” control disables the GPU callback, following the CPU path selected by the former filtering gate. The “after” run enables GPU on identical settings and geometry. This is a controlled comparison in one binary, not a separately compiled historical executable. Source import and GPU startup are excluded. Map-job time includes surface preparation, full-resolution raster/nearest interpolation, evaluation, filtering and immutable map storage. Total time additionally includes the caller's initial atlas construction.

| Size | Map | CPU job (s) | GPU job (s) | Job ratio CPU/GPU | CPU total (s) | GPU total (s) |
|---|---|---:|---:|---:|---:|---:|
| 2K | AO | 39.88 | 14.34 | 2.78× | 40.18 | 14.64 |
| 2K | Thickness | 80.19 | 15.57 | 5.15× | 80.49 | 15.87 |
| 2K | Curvature | 2.29 | 2.31 | 0.99× | 2.59 | 2.61 |
| 4K | AO | 174.66 | 73.11 | 2.39× | 185.86 | 74.20 |
| 4K | Thickness | 336.76 | 75.23 | 4.48× | 337.85 | 76.34 |
| 4K | Curvature | 29.16 | 30.93 | 0.94× | 30.27 | 32.03 |

All six GPU benchmark jobs logged GPU maps with no fallback; CPU controls logged no GPU maps. This was one sequential trial per configuration, CPU then GPU, so cache state and scratch I/O affect the ratios. Curvature showed **no end-to-end speedup** on this character. CPU topology/rasterization, nearest interpolation, ray generation/BVH construction and image storage still remain; GPU Pointiness moves field evaluation only. Large optional smoothing radii and other GPUs were not benchmarked. OIDN is not included in these Draft timings and remains CPU in Final. Shader/driver capability or allocation failures retain full-resolution CPU fallback.

All 12 native benchmark jobs completed and the application shut down cleanly. The wrapper was edited while that long batch was running, causing a shell EOF diagnostic after native completion. The current wrapper passed syntax checking and a separate benchmark rerun with exit 0; the complete original timing records were preserved.

Artifacts, relative to `~/mpainter-work/`:

- `REPORT-gpubake.md`, also copied into `mpainter/docs/gpubake/REPORT.md`.
- `mpainter/docs/gpubake/phase-one.json`, `benchmarks.json`, `surface-parity.json`.
- `shots/gpubake/draft-panel.png`, `final-dialog.png`.
- `shots/gpubake/character-{2048,4096}-{3,4,6}-{cpu,gpu}.png`, linear-map thumbnails; matching `.json` timing records. Roles 3/4/6 are AO/Curvature/Thickness.
- `build/gpubake/` contains build logs, `phase-one.log`, `benchmarks.log`, `units.log`, `legacy-gpu.log`, `mailbox-gpu.log`, `verification.log`, `wrapper-check.log`, and fresh `surface-cpu/` and `surface-gpu/` RGBA32F maps.

Reproduce the new GPU/UI proof and all character timings sequentially:

```sh
distrobox enter bts-build -- bash /mnt/mpainter-phase1/mpainter/scripts/host/test-gpubake-headless.sh
distrobox enter bts-build -- env BTS_GPU_PHASE1_BENCHMARKS=1 bash /mnt/mpainter-phase1/mpainter/scripts/host/test-gpubake-headless.sh
```

`BTS_GPU_PHASE1_SIZE=2048|4096`, `BTS_GPU_PHASE1_ROLE=3|4|6` and `BTS_GPU_PHASE1_GPU=0|1` select one benchmark. `BTS_GPU_PHASE1_TEST=0` runs the existing GPU ray suite instead; add `BTS_BAKE_JOB_GPU_TEST=1` for its fallback/cancellation proof. `build/gpubake/verify-rest.sh` records the exact remaining sequential test commands. For CPU surface tests, unset `BTS_SURFACE_GPU`; the native proof checks whether it is present, not whether its value is zero.

After those artifacts exist, validate them with:

```sh
distrobox enter bts-build -- xvfb-run -a python3 /mnt/mpainter-phase1/mpainter/scripts/host/verify-gpubake.py --root /mnt/mpainter-phase1
```
