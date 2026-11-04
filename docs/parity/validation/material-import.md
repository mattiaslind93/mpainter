# Material import and thumbnails

Final build SHA-256: `265471250792a73483e89e19ca15d146a694ad0a35120a8e8cb671957b77a65b`.

All 12 generated material cases passed on the RTX 3090 inside bts-build/private Xvfb. The checked-in generator and launcher were run end to end. Each final screenshot waits for actual EEVEE shader/texture queues to clear and sampling to finish; the explicit framebuffer read reports complete/error 0. No visible user desktop was used. Final evidence is in `docs/parity/validation/material-import.json` and `normal-import-comparison.png`.

## Changes

- mfPainter already rejected linked Principled Normal on its simple material import path. Complex imports now automatically use its retained native graph/fragment renderer. Unit-strength tangent Non-Color normal images import directly into Normal channel 3; application also enables the Texture Set Normal channel, which the project previously left disabled.
- Native Normal Map handles tangent strength, object/world space and the captured object transform. Bump retains strength, distance, invert and procedural/image Height inputs; normal-map/Bump chains retain Blender's nodes. Diffuse and other supported BSDF normal inputs use the same extraction path.
- Unsupported normal/color contexts and Shader-to-RGB closure inputs preserve the other channels and provide accurate warnings. Unsupported scalar Bump is disabled when its normal graph cannot be evaluated. GPU allocation/compiler failures are not hidden behind a generic fallback.
- Coat Weight, Roughness, IOR, Tint and Coat Normal are copied into the live Principled shader, with owned image copies. Node changes update the live Main so Blender rebuilds image dependencies. External object/collection IDs are rejected before copying unsupported coat branches. Material hide/replacement clears previous coat state, including constant coat.
- Deleted preview environment resources had made all thumbnail illumination zero. The shaderball preview now uses the application's own mathematical studio illumination. The actual reported `Brand Test - Turquoise Copper Noise` source and its reproduced graph both generate nonblack turquoise/copper shaderballs.

## Verification

| Cases | Result |
| --- | --- |
| Image normal, procedural bump, bump + normal map | Channel 3 varies: ranges 0.293105, 0.084665, 0.264809 |
| Coat Normal, constant coat | Weight survives project save/open; hiding material resets Weight to zero |
| Object normal, world normal, Diffuse normal | Native normal output completes with no error |
| Unsupported normal, Geometry True Normal color, Shader-to-RGB color | Supported remainder imports/renders; explicit capability warning preserved |
| Brand Test - Turquoise Copper Noise | Shaderball center RGB 147, 103, 87; visible procedural turquoise/copper pattern |

The four comparison rows were visually inspected: image-normal waves, procedural bump, combined waves/bump and separate coat highlights are visible. Blender references use Cycles CPU with area lights; mPainter uses EEVEE with Studio HDR. Both use the same exported smooth sphere mesh. Lighting/camera differ, so these are qualitative comparisons, not pixel-equality claims.

An earlier test captured unfinished rendering and once crashed in a TBB worker during rapid project changes; its crash backtrace did not establish a specific owner. GDB repeated the coat/object/save/open sequence cleanly. Static review found and corrected the live coat tree's missing Main dependency update. The corrected code then passed all 12 cases with the original rapid 24-frame switching and no completion wait (`material-import-rapid.json`), as well as the complete final ready-frame suite. The normal project reset frees render targets and the old depsgraph before old IDs; EEVEE image decoding joins its TBB work inside the owner draw. No speculative rendering changes were made.

The complete 231-test CPU suite passed on SHA `220d7dd81ace1970418d48aedba598811773d13f5970cd60db0fc5fb86a0990d` (`logs/parity-full-cpu-complete/results.json`). The material test includes supported Diffuse Normal, retained Coat controls, and unsupported Texture Coordinate From Instancer and reference-object inputs. Its former requirement for a missing Color channel was updated to the requested partial-import behavior: a constant-only neutral output, preserved source-context error, explicit user warning, unchanged source graph and no GPU compilation.

The real UI async-open check exposed a stale layer-panel snapshot: material selection refreshed while `opening_project` was still true, and completion never refreshed the panel. The completion callback now synchronizes the material controls after clearing that flag. This changes the UI completion notification, not project hydration or rendering.

The real reopened Coat project also reached **Cycles / OPTIX / Rendering Done** on RTX 3090 with production SHA `b92130ae25d81b535bcc953702d6ba478ff787573b404b480a5a788c7b98663c`. This exposed and fixed two existing image-publication assumptions: packing changes owned generated composites into FILE images, so only exact local packed composite bindings are restored to GENERATED while retaining CPU/GPU pixels; disabled private 1×1 world-normal sentinels are no longer registered as completed authored images. Actual normals remain in the full-resolution composite channel 3, and strict mirror identity validation remains intact. The copied Coat texture is an ordinary source image and is not included in this normalization. Evidence: `build/parity-ui-render-check/cycles-optix-complete.png` and `ui-proof.json`.

## Scope

Coat remains a material-level shader property supplied by the top visible material Fill, matching the application's existing global shader-property model. It is not a separately masked coat channel for each layer. Original source graphs remain archived. Unsupported channels are explicit fallbacks, not a claim of arbitrary Blender shader parity.

## Reproduce

Run from the repository on the host:

```sh
scripts/host/test-material-import-headless.sh --generate
scripts/host/test-material-import-headless.sh --rapid
```

The launcher always uses bts-build, `env -u WAYLAND_DISPLAY`, `xvfb-run -a`, the private-session wrapper and VirtualGL's EGL RTX 3090 path. Override `MPAINTER_TEST_ROOT`, `MPAINTER_TEST_LAUNCHER`, `MPAINTER_TEST_BLENDER`, `MPAINTER_TEST_FIXTURES` or `MPAINTER_TEST_OUTPUT` only for alternate test locations. No GPU memory-budget setting is involved.

The generator is `native/tests/fixtures/create_normal_materials.py`. It produces twelve `.blend` fixtures, packed Non-Color wave images, the shared OBJ and Blender reference PNGs. The Brand fixture reproduces the original reported graph. The original external source was independently verified; its proof and thumbnail are `original-brand-proof.json` and `original-brand-thumbnail.png`.

Generate the comparison after a completed ordinary run:

```sh
distrobox enter bts-build -- env -u WAYLAND_DISPLAY xvfb-run -a \
  /mnt/mpainter-phase1/mpainter/scripts/host/run-parity-headless.sh \
  python3 /mnt/mpainter-phase1/mpainter/native/tests/fixtures/compare_normal_materials.py \
  /mnt/mpainter-phase1/logs/material-import
```

Full local logs: `logs/material-import/runtime-reproducible.log`, `runtime-rapid.log`, `runtime-ready.log`, `runtime-repeat.log`, and `runtime-gdb.log`. No bundled material libraries or Adobe mask files are added by these fixtures.
