# third_party

Everything mPainter takes from outside and changes or bundles. `blender/` holds the Blender patches. `optix/` holds the license files for the OptiX SDK that GPU baking is built against. The SDK itself is not in the repository.

## Blender

- The pinned Blender is v5.2.0, commit `fbe6228777e7d9afefcd61a413844e790ae75db7`, recorded in `native/build-pins.json`. `scripts/prepare-blender.py` clones it and refuses any other commit.
- Blender sources change only through the numbered patches in `blender/patches/`. A patch applies to every platform. Never put a Blender change in a platform build script.
- `scripts/prepare-blender.py` applies `0001-native-host.patch` to a clean tree, then every other numbered patch in sorted order. Each patch gets its own receipt in Blender's `.git` folder. A patch that is already present counts as applied. A changed patch stops the script.

| Patch | Why it exists |
|---|---|
| `0001-native-host.patch` | Includes the host build and adds the GPU, EEVEE, window-manager, image and Cycles hooks the host needs. It changes 67 Blender files and adds 13 headers. |
| `0002-windows-creator-ansi.patch` | Calls the ANSI Windows functions (`GetModuleHandleExA`, `GetModuleFileNameA`) in Blender's crash handler, because the code passes narrow strings |
| `0003-windows-host-tbbmalloc-proxy.patch` | Stops `tbbmalloc_proxy` from replacing the C runtime's allocator, which crashed the Qt host before `main` |

## Adding a patch

1. Take the next number. Name the file `<number>-<platform or area>-<what>.patch`.
2. Start the patch with one line that says why it is needed and whether it could go upstream to Blender.
3. Keep it to one purpose. A smaller patch survives a Blender upgrade better.

The macOS Metal patches come from the Mac Studio's `mac-port` branch, where they are numbered 0002 to 0007. They become 0004 to 0009 when they land, because 0002 and 0003 are already taken here.

## Upgrading Blender

Phase 6 of `docs/plans/2026-11-08-restructure.md` splits patch 0001 by purpose and adds an upgrade command. Until then, an upgrade means a new pin, every patch reapplied by hand, and the full test suite on every platform.
