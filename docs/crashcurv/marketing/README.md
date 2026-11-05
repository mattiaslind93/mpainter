# mPainter marketing drafts

Local draft package, 4 November 2026. Nothing pushed, published or uploaded. The application repository was read only. Both store drafts were audited against local reports and source; [CHANGES.md](CHANGES.md) records corrections and evidence.

## Images

Four fresh, unretouched **1920×1080 PNG application captures** are in `screens/`:

1. `01-materials-cherry-wood.png`: Shader_Ball_JL_01 with the built-in Cherry Wood material from Poly Haven, UV mapping, 1024 texture resolution, and Assets > Materials filtered to Built-in. The overall 48-material counter includes the project's retained copy; the built-in library has 47.
2. `02-triplanar-mapping.png`: the same material in Triplanar mode, with the actual projection frame and controls visible.
3. `03-texture-sets.png`: Texture Sets, the set checkbox, UV tile 1001 checkbox and 1024 texture size.
4. `04-position-height-mask.png`: Position height generator on the shaderball, with its actual World Height controls and enabled provisional 3D surface fallback. This is not a baked-position preview.

[gumroad-cover-1280x720.png](gumroad-cover-1280x720.png) and [gumroad-thumb-600x600.png](gumroad-thumb-600x600.png) use viewport crops from screenshot 01. They preserve the real rendered surface and contain the supplied mPainter wordmark, ALPHA and 0.9.0. Only crop, proportional resize, layout and wordmark accent recoloring were used. No synthetic product imagery or reconstructed UI. The cover uses near-black, off-white, a fine rule and #22947D. Both exports have sRGB profiles. See `evidence/composition.json` for exact crops and contrast checks, and `evidence/artwork-review-300.png` for the small-size review.

## Capture and verification

The current `../dist/mPainter-0.9.0-alpha-linux-x86_64.AppImage` was used directly with extract-and-run. Its SHA-256 and repository revision are in `evidence/inputs.json`. Every app run used a private `xvfb-run` display inside **distrobox bts-build**, Qt xcb, VirtualGL EGL and isolated config/data/cache/recovery directories. The app logs identify **NVIDIA GeForce RTX 3090** rendering. No software-rendering fallback was needed. No visible desktop windows were opened.

Runs were sequential. No build was needed, no heavy test/build jobs overlapped, and OMP was limited to four threads. The existing Poly Haven UI path verified 47 built-in materials, 1024 texture dimensions, UV/Triplanar changes and saved-project reopening, then exited cleanly. The final interactive capture session also exited cleanly.

A bake issue was observed in the first interactive session: all seven maps completed at 64×64, including GPU AO/thickness, but a subsequent 256×256 rebake crashed after cache release, exit 139. The crash and full log are retained. Follow-up on 5 November traced the crash to invalid corner-normal fan indexing on collapsed geometry. The local fix passed sequential 64, 256 and 1024 rebakes in both directions, including GPU AO/thickness in the workspace. See [the crash and Pointiness report](../REPORT-crashcurv.md) and [the comparison](../crashcurv.png). The current packaged AppImage predates this fix; the updated store drafts state that packaging is pending. No new 4K/8K test was run. There is no baked-map feature screenshot among the four selected images; the completed small bake is evidence only.

The first launch attempt stopped at the app's private-display safety guard. The isolated config path was corrected to its supported convention and the guard was retained. Accessibility retries and the initial project-open correction are retained in the command histories. No app source was edited.

All four screenshots and both artwork sizes were visually inspected. The cover and thumbnail were also inspected at 300 px. Primary branding remains clear; the cover's small supporting labels are intended for full-size reading. `evidence/verification.json` records dimensions, hashes, contrast results and repository status.

## Credits

Shader Ball JL 01 by **JLRIN / Craneo (CRANEO_)**, [original model](https://sketchfab.com/3d-models/shader-ball-jl-01-9ecc1d8ddb5b4059a80d0bd925e3dd3b), [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/). The app uses an upright OBJ conversion and derived preview maps. These images are new application captures with materials applied; the cover and thumbnail are cropped and resized compositions.

Cherry Wood uses **cherry_veneer** texture maps from [Poly Haven](https://polyhaven.com/a/cherry_veneer), CC0, through the bundled material preset. Default Studio HDR is by Mattias Lind. The wordmark is Mattias Lind's supplied m mark with Inter Bold lettering. Supporting type is the installed Noto Sans and Noto Sans Mono. No font binaries are redistributed. The visual references from the supplied design skill were used for hierarchy and spacing only; no reference artwork appears in these images. These credits accompany all four screenshots and both compositions.

## Local helpers

Recompose the two artwork files and 300 px review from screenshot 01:

```sh
python3 scripts/build-artwork.py
```

`headless-env.sh` prepares the isolated container environment. `capture-ui.py` opens the retained `evidence/cherry-wood.blend` project in the current AppImage on that private display and accepts local JSON commands under `.capture/commands/`. It is an interactive capture helper, not a headless regression suite. The actual command histories, including retries, are in `evidence/`. Only run the helper through:

```sh
distrobox enter bts-build -- bash /mnt/mpainter-phase1/marketing/scripts/headless-env.sh dbus-run-session python3 /mnt/mpainter-phase1/marketing/scripts/capture-ui.py
```

The container path uses the existing `/mnt/mpainter-phase1` work-volume bind mount. Original shaderball/material assets and brand masters remain outside this draft directory. The scripts use local Pillow, system fonts and existing container tools. Nothing requires an account or upload. Temporary capture caches and extracted app files were removed after capture.

The two Gumroad drafts are audited. `posts.md`, `video-script-30s.md` and `pricing-rationale.md` remain the supplied drafts and have not received the same feature audit. Proposed prices, updates through 1.0 and the refund placeholder are author decisions.

## Complete file inventory

Exact bytes below include this README, supplied drafts, supplied design-skill files, selected images, scripts and retained evidence.

| File | Bytes | What it is |
| --- | ---: | --- |
| [CHANGES.md](CHANGES.md) | 8,375 | Bilingual correction log with local source evidence and observed rebake issue |
| [README.md](README.md) | 12,584 | Delivery notes, credits, reproduction and complete inventory |
| [design-skill/SKILL.md](design-skill/SKILL.md) | 5,501 | Supplied design-language skill, unchanged |
| [design-skill/agents/openai.yaml](design-skill/agents/openai.yaml) | 263 | Supplied design-skill support file, unchanged |
| [design-skill/assets/reference-gallery.html](design-skill/assets/reference-gallery.html) | 10,784 | Supplied design example or supporting asset, unchanged |
| [design-skill/assets/specimen.html](design-skill/assets/specimen.html) | 15,324 | Supplied design example or supporting asset, unchanged |
| [design-skill/assets/tokens.json](design-skill/assets/tokens.json) | 1,838 | Supplied canonical design tokens, unchanged |
| [design-skill/assets/web.css](design-skill/assets/web.css) | 3,854 | Supplied design example or supporting asset, unchanged |
| [design-skill/references/components.md](design-skill/references/components.md) | 4,298 | Supplied design reference: components |
| [design-skill/references/documents-slides.md](design-skill/references/documents-slides.md) | 3,796 | Supplied design reference: documents slides |
| [design-skill/references/foundations.md](design-skill/references/foundations.md) | 5,048 | Supplied design reference: foundations |
| [design-skill/references/image-index.md](design-skill/references/image-index.md) | 6,316 | Supplied design reference: image index |
| [design-skill/references/layouts.md](design-skill/references/layouts.md) | 3,351 | Supplied design reference: layouts |
| [design-skill/references/motion.md](design-skill/references/motion.md) | 5,029 | Supplied design reference: motion |
| [design-skill/references/platforms.md](design-skill/references/platforms.md) | 3,473 | Supplied design reference: platforms |
| [design-skill/references/quality.md](design-skill/references/quality.md) | 2,407 | Supplied design reference: quality |
| [design-skill/references/sources.md](design-skill/references/sources.md) | 4,847 | Supplied design reference: sources |
| [design-skill/scripts/check_tokens.py](design-skill/scripts/check_tokens.py) | 1,993 | Supplied design-skill support file, unchanged |
| [evidence/artwork-review-300.png](evidence/artwork-review-300.png) | 137,535 | Cover and thumbnail at 300 px for visual review |
| [evidence/bake-64-complete.png](evidence/bake-64-complete.png) | 186,157 | Actual completed seven-map 64×64 bake dialog |
| [evidence/bake-settings.png](evidence/bake-settings.png) | 218,876 | Actual 256×256 rebake settings before the crash |
| [evidence/branding.json](evidence/branding.json) | 395 | Current AppImage version and startup capture receipt |
| [evidence/cherry-wood.blend](evidence/cherry-wood.blend) | 25,587,201 | 25 MB native project generated by the current AppImage |
| [evidence/composition.json](evidence/composition.json) | 790 | Crop bounds, source hash, wordmark treatment and contrast ratios |
| [evidence/inputs.json](evidence/inputs.json) | 1,939 | Input AppImage hash, repository baseline and report hashes |
| [evidence/mesh-rebake-crash.txt](evidence/mesh-rebake-crash.txt) | 1,054 | Native crash backtrace from the rebake |
| [evidence/polyhaven-initial-guard.log](evidence/polyhaven-initial-guard.log) | 65 | Initial safe launch rejection, preserved |
| [evidence/polyhaven-run.log](evidence/polyhaven-run.log) | 21,413 | Successful fresh 1K material/save/reopen run |
| [evidence/polyhaven-validation.json](evidence/polyhaven-validation.json) | 579,642 | App-generated 1K material, mapping and reopen evidence |
| [evidence/ui-command-history.json](evidence/ui-command-history.json) | 2,629 | Final mask capture commands and results |
| [evidence/ui-controller.log](evidence/ui-controller.log) | 4,603 | Final private session/controller diagnostics |
| [evidence/ui-exit.json](evidence/ui-exit.json) | 34 | Final app exit status |
| [evidence/ui-first-command-history.json](evidence/ui-first-command-history.json) | 10,062 | First session commands, outcomes and retries |
| [evidence/ui-first-controller.log](evidence/ui-first-controller.log) | 4,603 | First private session/controller diagnostics |
| [evidence/ui-first-exit.json](evidence/ui-first-exit.json) | 36 | First interactive app exit status, 139 |
| [evidence/ui-first-run.log](evidence/ui-first-run.log) | 275,334 | First interactive capture and failed rebake log |
| [evidence/ui-run.log](evidence/ui-run.log) | 15,403 | Final mask capture session app log |
| [evidence/verification.json](evidence/verification.json) | 3,601 | Final artifact dimensions/hashes, review and repository checks |
| [gumroad-cover-1280x720.png](gumroad-cover-1280x720.png) | 562,717 | 1280×720 screenshot-based cover, sRGB |
| [gumroad-en.md](gumroad-en.md) | 5,153 | Corrected English store draft |
| [gumroad-sv.md](gumroad-sv.md) | 5,301 | Corrected Swedish store draft |
| [gumroad-thumb-600x600.png](gumroad-thumb-600x600.png) | 309,836 | 600×600 screenshot-based thumbnail, sRGB |
| [posts.md](posts.md) | 1,156 | Existing social post drafts, unchanged |
| [pricing-rationale.md](pricing-rationale.md) | 574 | Existing pricing rationale, unchanged |
| [screens/01-materials-cherry-wood.png](screens/01-materials-cherry-wood.png) | 1,295,165 | 1920×1080 real app capture: Cherry Wood and built-in Materials |
| [screens/02-triplanar-mapping.png](screens/02-triplanar-mapping.png) | 1,306,736 | 1920×1080 real app capture: Triplanar mapping |
| [screens/03-texture-sets.png](screens/03-texture-sets.png) | 1,278,178 | 1920×1080 real app capture: set and tile checkboxes |
| [screens/04-position-height-mask.png](screens/04-position-height-mask.png) | 1,174,109 | 1920×1080 real app capture: Position height with 3D fallback |
| [scripts/build-artwork.py](scripts/build-artwork.py) | 4,207 | Local screenshot composition and contrast verification |
| [scripts/capture-ui.py](scripts/capture-ui.py) | 6,150 | Owned-process UI automation and real X11 capture helper |
| [scripts/headless-env.sh](scripts/headless-env.sh) | 1,239 | Isolated Xvfb/VirtualGL environment inside bts-build |
| [scripts/write-inventory.py](scripts/write-inventory.py) | 10,431 | Regenerates this README and exact file sizes |
| [video-script-30s.md](video-script-30s.md) | 886 | Existing 30-second video script, unchanged |
