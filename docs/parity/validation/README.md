# Validation artifacts

These are results from the actual native application and RTX 3090, not simulated render images. All runs used `bts-build`, `env -u WAYLAND_DISPLAY xvfb-run -a`, private configuration/resource/recovery directories and, for OpenGL, VirtualGL with its EGL backend. No test used display :0 or :1. The original artist session was not controlled.

`../memory-verification.md` describes the memory/baking fixtures, exact comparisons and their limits. `theme-image-colors.json` compares interface color values with the supplied Blender screenshot. The Mask Generator CPU/GPU sheets and actual UI capture cover all ten catalog entries.

The diagnostics below select test actors only. Their injected memory values never become production settings or preferences. `BTS_BAKE_TEST_RESOLUTION` selects the fixture size; project resolution remains authoritative in normal use.

| Proof | Command / diagnostic environment |
| --- | --- |
| Production GPU/CPU/cancellation | `--headless-viewport-test OUTPUT`, `BTS_GPU_BAKE_BVH_TEST=1 BTS_BAKE_JOB_GPU_TEST=1` |
| Reusable cache reclaim | same command, `BTS_GPU_BAKE_BVH_TEST=1 BTS_BAKE_CACHE_TEST=1` |
| 4K/8K all-ray equality | same command, `BTS_GPU_BAKE_BVH_TEST=1 BTS_FULL_RES_BAKE_TEST=1` |
| All finished maps | same command, `BTS_BAKE_MEMORY_STRESS=1 BTS_BAKE_TEST_RESOLUTION=4096` or `8192` |
| 8K EXR + displacement install | `--cpu-texture-import-test OUTPUT`, `BTS_HEIGHT_8K_IMPORT_TEST=1` |
| All ten GPU generators | `--headless-viewport-test OUTPUT`, `MPAINTER_MASK_GENERATOR_TEST=1` |
| Strict CUDA and OptiX render | `--headless-cycles-cuda-test OUTPUT` |

Run the executable through `scripts/host/run-parity-headless.sh` **inside** the private Xvfb session. That wrapper isolates writable state and verifies the display. OpenGL diagnostics require the `gpu-env.sh` setup and `VGL_EGLLIB=/usr/lib64/libEGL.so.1 vglrun -nodl -d egl -c proxy`; CUDA/OptiX compute diagnostics need no VirtualGL routing. The release AppImage supplies its own runtime libraries and resolves only NVIDIA driver libraries from the host.

The complete 4K reference uses one GPU batch; the 8K reference uses two because of the hardware SSBO limit. Both compare all output bits against 16 MiB diagnostic batching, without reducing dimensions. Low-memory CPU fallback is separately compared with the production CPU oracle. Screenshots demonstrate visual behavior; numerical JSON assertions provide the exactness checks.

Final delivery: `appimage.json` records the released artifact hash. `final-appimage-ui.json` and the three `*-final-ui.png` captures verify asynchronous Open, all ten visible generators, completed Auto/OptiX rendering of a reopened Coat project, and restoration of the painting workspace. `cpu-suite.json` contains the complete 231-check pass; `cpu-final-targeted.json` records three affected checks rerun on the final executable after the last Cycles repair.
