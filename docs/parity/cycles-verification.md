# Cycles GPU parity investigation

All application runs used `distrobox enter bts-build -- env -u WAYLAND_DISPLAY xvfb-run -a`. No real display was used. `/home/mlind/bts` was read only.

## Reproduced root causes

The original AppDir failed its `--headless-cycles-cuda-test`. `logs/cycles-baseline/run.log` records `CUEW initialization failed: Error opening the library`. The container has NVIDIA device nodes but no `/usr/lib64/libcuda.so.1`; the host's 615.71.09 driver is available at `/run/host/usr/lib64/libcuda.so.1`. Direct `cuInit(0)` with the host library succeeds and reports one device.

The release build disabled CUDA kernels and OptiX. `scripts/package-linux.py` additionally deleted `5.2/scripts/addons_core/cycles/lib`. Cycles' CUDA discovery rejects a device when neither that kernel directory nor nvcc exists, so missing kernels also produced the misleading no-CUDA message.

The mfPainter reference compiled CUDA sm_86 and installed its cubin. Its cache requested OptiX, but `OPTIX_INCLUDE_DIR-NOTFOUND` means it did not establish working OptiX support.

## Changes

- `scripts/build.py` enables CUDA kernels and OptiX, with CUDA/OptiX SDK arguments. Native sm_86 and compute_75 PTX support RTX 3090 and forward JIT.
- `scripts/package-linux.py` retains kernels and requires CUDA/OptiX release resources. It excludes libcuda, libnvoptix, and NVIDIA driver libraries even if they occur in runtime/lib.
- AppRun normally uses system driver search. In Distrobox, it creates temporary links to only the host NVIDIA driver libraries and removes those links after exit. It does not inject the host's libc into the package search path or copy/bundle drivers.
- The OptiX SDK license notices are retained in `third_party/optix` and the package license directory.
- Cycles preview tries OptiX then CUDA and falls back to CPU if neither is available. The UI displays the actual backend.
- The retained headless GPU diagnostic now requires separate CUDA and OptiX GPU renders and rejects missing devices, CPU fallback, or blank images.
- Final interactive checks exposed two additional path-specific issues: the changed workspace layout hid the viewport instead of the Assets shelf on entering Cycles, and packed Save/Open converted owned generated composites to FILE images that the CPU-mirror publisher correctly rejected. The controller now hides the semantic Assets widget; only validated, local, binding-owned packed composites regain GENERATED source classification while retaining existing buffers. Imported file textures, including Coat images, are excluded. A follow-up exact identity diagnostic identified disabled legacy world-normal zero sentinels (1×1 GPU input with project-sized generation metadata) being registered as authored products. These permanently disabled legacy inputs are no longer registered with the Cycles mirror; the actual layered normal channel remains at full project resolution and is still mirrored. The image validator remains strict.

## Verification

`logs/cycles-driver-probe.log` and `logs/cycles-driver-probe/result.json` prove that the launcher and preserved kernel fixes enumerate `NVIDIA GeForce RTX 3090` and render a nonblank 64x64 image on CUDA. This used the old executable to isolate packaging causes. All 24 CUDA/OptiX kernel build actions succeeded (`logs/cycles-kernels-build.log`), including native sm_86 cubin, forward-compatible compute_75 PTX, and OptiX kernels with OSL support. The kernels occupy 12 MiB compressed and have been installed in the workspace runtime. The rebuilt executable passed the same strict diagnostic through the generated AppRun: CUDA and OptiX each enumerated `NVIDIA GeForce RTX 3090`, selected `GPU`, and rendered a nonblank 64x64 image at 8 samples. Times were 0.625 s and 1.939 s. `logs/cycles-rebuilt.log` and `logs/cycles-rebuilt/result.json` record the result. Both PNGs were visually inspected; they show the expected lit factory cube and have identical RGBA pixels. Final AppImage verification remains assigned to the packaging pass.

The actual active build directory is `build/linux-native`, accessed through `/mnt/mpainter-phase1` in the container. The stale `build/linux` cache has unsuitable symlink paths. `logs/cycles-configure-native.log` records a successful configuration with CUDA 13.2 and OptiX 9.1. NVIDIA's official headers are pinned to tag v9.1.0, commit `f1f6dd803f3159992d248178f6e09421c6eb8b6d`, under workspace `toolchains/optix-9.1.0`. Source: https://github.com/NVIDIA/optix-dev/tree/v9.1.0 . The existing CUDA toolkit is only read.

Principles applied: Fix Root Causes led to testing driver discovery and kernel discovery independently; Model the Domain represented backend preference as an ordered candidate list; Prove It Works requires actual GPU renders from the deliverable.

The executable used for the first rebuilt CUDA/OptiX proof had SHA256 `3aa6249bb4bbdf4c30ae7cd612a0bcee98d266c8257ba796c7e9b0392c1252b8`. Later material and memory fixes supersede that binary; this SHA identifies the retained early proof, not the final deliverable. The final AppImage run will establish the deliverable identity. Host compile/link and focused release checks passed. The full restored CPU umbrella built all 200 native executables. The first complete 231-check manifest run passed 223 checks and exposed eight fixture/resource/lifecycle issues. Their causes and corrections are recorded in `logs/cpu-suite.md`. The next full run passed 230 of 231; remaining material fixtures still expected refusal where partial import or supported Coat/Diffuse Normal paths are now required. After correcting those assertions, the final full run passed **231 of 231**, exit 0 (`logs/parity-full-cpu-complete/results.json`), on executable SHA256 `220d7dd81ace1970418d48aedba598811773d13f5970cd60db0fc5fb86a0990d`. All 20 independent Python/script manifest checks are included and pass. Interactive Cycles saved-project identity diagnostics continue separately from this CPU result.

`logs/cycles-kernel-manifest.json` records all eleven installed kernel hashes, the actual Cycles source-tree digest, SDK provenance and license equality. The copied Blender source has no Git metadata, so the intended release pin is explicitly distinguished from the measured tree digest. `logs/parity-qt-hashes.json` establishes that the actual QtCore, QtGui and QtWidgets 6.8.3 libraries are byte-identical to mfPainter's libraries; no Qt rebuild was performed.

Final targeted CPU checks after excluding disabled legacy world sentinels all passed: native-material UI, native Cycles revisions, and native Cycles publication. `logs/parity-cycles-final-cpu/results.json` records executable SHA256 `b92130ae25d81b535bcc953702d6ba478ff787573b404b480a5a788c7b98663c`. The 231-check full run above remains attributed to its earlier binary. The final packaged GPU/UI proof is still recorded by the root packaging pass.

Root also verified the actual interactive rendering path on `b92130ae...` under private Xvfb: reopening the Coat Normal project, selecting Rendering, automatic RTX 3090 OptiX selection, and reaching `Rendering Done` with the correctly shaded turquoise sphere and Studio environment. Evidence: `build/parity-ui-render-check/cycles-optix-complete.png` and `build/parity-ui-render-check/ui-proof.json`. This closes the saved-project UI regressions separately from the isolated CUDA/OptiX smoke test.

Final delivery AppImage SHA256 `7823a85003885fc5279563e7ece86fb48aa04213fb7b1961275f7e40066ad334`: both CUDA and OptiX strict renders PASS on RTX 3090; decoded RGBA pixels byte-identical (PNG metadata differs). See `validation/cycles-appimage.json`.
