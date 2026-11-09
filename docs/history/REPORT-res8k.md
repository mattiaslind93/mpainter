# Resolution, dropdowns, and material shelves

All work is local on `main`. Builds use at most six workers, with only one build
or heavy test process at a time. The container has a hard 6 GiB memory limit.
After detecting page-reclaim
stalls at six workers, the build resumed with two workers. No OOM kill occurred.
GUI verification runs under a private Xvfb server inside `distrobox bts-build`.
Nothing was pushed, published, or uploaded.

## Dropdown root cause

The regression is in `OptionStyle::drawControl`, introduced by the global
checkbox treatment in `41d2be9`. Qt's default combo popup uses `CE_MenuItem`
and checkable menu options without reserving an indicator column. The theme
painted another 16-pixel checkbox at the left of every row, covering its text.
The original live popup showed `1024` as `24` and `8192` as `92`.

The fix delegates combo rows to Qt and keeps the custom indicators for actual
checkable menus, checkboxes, radio buttons, and item views. The stylesheet's
checkbox sizes and slider hit areas are unchanged.

The rendering regression opens actual default, editable, and QListView-backed
combo popups in both layout directions. It checks that every visible row has
text pixels and no checkbox-sized area of indicator fill. A small tolerance
allows antialiased glyph pixels that happen to share the same gray. Existing
tests still cover enabled,
disabled, unchecked, checked, mixed, and hovered indicators and checkable menus.

The initial failing run reported:

```text
Dropdown row 0: text=15 indicatorFill=84 area=7400
Assertion `inner == 0' failed.
```

After the fix, the default popup reported 57 text pixels and zero indicator-fill
pixels on its first row. The before/after captures and logs are in
`build/res8k-dropdown-before.png`, `build/res8k-dropdown-after.png`, and
`build/res8k-theme-{before,after}.log` beside the repository.

## Resolution limit

The maximum selectable or accepted project, texture-set, tile, bake, and export
resolution is 8192 pixels per dimension. Shared resolution menus never offer
larger choices. Oversized stale menu selections clamp to 8192 with an explanatory
tooltip. External settings and project metadata reject oversized values with an
error naming `8192 (8K)` before texture pixel allocation. Python automation and CLI input use
the same cap.

The sparse pixel storage representation and imported source-image storage remain
independent of the output resolution cap. Bake supersampling can use a larger
internal working extent. These are not selectable project or output sizes.

Tests exercise 8192 acceptance and 8193, 16384, and integer-limit rejection,
including rectangular dimensions, tile overrides, saved project metadata, bake
settings, and automation export planning. Rejected edits preserve the document.

## Materials vs Material Presets

Materials contains plain materials with no dynamic smart masks. This includes
Poly Haven, procedural built-ins, CraftyPack, imported Blender materials, and
saved folders without mask generators.

Material Presets contains materials with smart masks or mask generators. Saved
folders qualify when a generator occurs anywhere inside them, including nested
mask groups. Classification uses decoded content rather than names, manually
assigned tags, filenames, or the save command used. Static bitmap or painted
masks alone do not turn a material into a dynamic preset.

Existing library entries are reclassified from their contents on load without
changing original material blobs, IDs, or hashes. Original SPSM archives use
metadata-only classification, with a cache bound to their bytes and interpretation
policy; image hydration remains deferred to the existing worker. A folder with generators saved through Save as
Material uses the editable preset archive so its dynamic masks survive.

## Implementation choices

The pstack Model the Domain principle guided two shared rules: the explicit
`max_texture_resolution` constant and the `MaterialShelf` content classifier.
Codec usage markers remain intact because they select the archive reader and
application path; changing them merely to choose a shelf would break editable
plain folders. An independent review found the legacy mask-archive entry point,
which now checks the cap before reading paint pages.

## Verification and deliverables

- Linux build passed for `bts-host`, `bts-cpu-tests`, the theme, asset, mask,
  material-preset, and SPSM asset test targets. Log: `build/res8k-linux-build.log`.
- **235 CPU checks passed, 0 failed**, including native PNG export, project
  shutdown, workspace asset drag/drop, and the added rendering and cap checks.
  Results: `build/res8k-cpu-final/results.json`.
- Actual production resolution widgets passed for New Project, Workspace,
  Baking, and Skew with 16K, 32K, and unknown GPU limits. All stay capped at 8192.
  Results: `build/res8k-extent/verified.json`.
- The full application screenshot proof passed and shut down cleanly. It also
  verifies that CLI resolution 16384 exits with code 2 and an explicit 8K error.
  Results: `shots/res8k-proof.json`, `build/res8k-proof.log`, and
  `build/res8k-proof/cli-rejection.log`.
- Native metadata and recovery verification passed, including 8K export planning
  and existing large metadata/recovery cases. Results:
  `build/res8k-metadata/verified.json`.

The first CPU run exposed an overly strict pixel-color assertion. One
antialiased glyph pixel matched the indicator gray. The corrected check permits
fewer than 20 such pixels; the original overlay had 84 or more. A rerun in the
same output directory encountered retained file-browser pins. The final run used
a clean isolated directory and all 235 checks passed.

The 8K boundary tests use metadata and sparse pages without dense 8K GPU
allocations. The optional five-source external SPSM fixture suite was not run;
CPU tests cover source-cache invalidation, native lowering, and library migration.

All requested screenshots were captured from a private X server and visually
inspected. Real checkable blend-menu actions and Texture Sets retain visible
Turquoise Dark checks. The shelf proof uses an isolated test library, with 47
stock materials plus one saved plain folder in Materials and one saved folder
containing a World Height generator in Material Presets. The two lists have no
overlap. The user's actual library is not populated with these test fixtures.

Files under `~/mpainter-work`:

- `res8k.png`: New Project with the resolution dropdown open.
- `shots/dropdowns/new-project.png`
- `shots/dropdowns/texture-set.png`
- `shots/dropdowns/bake-settings.png`
- `shots/dropdowns/blend-mode.png`
- `shots/dropdowns/mask-generator.png`
- `shots/dropdowns/assets-filters.png`
- `shots/shelf-materials.png`
- `shots/shelf-material-presets.png`

## Reproduce locally

Run inside `distrobox bts-build`, using `/mnt/mpainter-phase1` as the workspace
root. Build targets sequentially with `cmake --build build/linux-native
--parallel 2 --target bts-host bts-cpu-tests bts-theme-tests`. The existing
container limit makes two workers appropriate for the large application files.

Run `scripts/host/test-res8k-headless.sh` from this repository to reproduce the
rendering checks, CLI rejection, and screenshots. It starts private Xvfb servers
and isolates its settings and library. Run `scripts/host/verify-cpu.py` under
`xvfb-run` with a **fresh** `--output` directory, using
`--build-dir /mnt/mpainter-phase1/build/linux-native`. The optional widget and
metadata checks are `verify-texture-extent-choices.py` and
`verify-native-metadata-budget.py`, also under `xvfb-run`.

The built application is `~/mpainter-work/build/linux-native/bin/mpainter`.
The shelf definition also appears in the repository's `docs/blender/masks.md`;
`docs/theme.md` documents the dropdown mechanism and regression test.
