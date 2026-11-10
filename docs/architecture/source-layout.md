# Source layout

How `native/` is organised while the restructure plan's phases 4 and 5 are under way.

| Folder | What lives there |
|---|---|
| `native/host/` | The application, compiled into `bts-host`. The top level holds only `CMakeLists.txt`, `SourceIncludeDirs.cmake` and `AGENTS.md` (with its `CLAUDE.md`); `main.cc` is in `app/`. |
| `native/host/<domain>/` | Domain folders: `app/`, `assets/`, `baking/`, `blender/`, `diagnostics/`, `document/`, `fill/`, `gpu/`, `io/`, `layers/`, `materials/layering/`, `materials/native/`, `materials/openpbr/`, `painting/`, `persistence/`, `platform/`, `python/`, `spsm-import/`, `ui/dialogs/`, `ui/panels/`, `ui/shell/`, `ui/theme/`, `ui/widgets/`, `viewport/`. Still to come from the plan: `materials/common/`. |
| `native/host/blender/` | The only folder allowed to include Blender headers (plan rule 7). Every native/host file that includes them is here, except `app/main.cc`. A file whose `.cc` includes Blender sits here while its header, which does not, stays in its domain. The files still outside are harness and test code; `mp check`'s `blender-boundary` ratchet counts them. |
| `native/host/openpbr/` | Vendored OpenPBR resources (Apache-2.0), not our code. |
| `native/harness/` | Test, demo and proof code (`*Demo`, `*Test`, `*Proof`, `*Fixture`, `*Witness`, ...). Still compiled into `bts-host`, because `main()` calls it. |
| `native/tests/` | Test programs and their fixtures. |
| `native/spsm/`, `native/sbsar_metadata/` | Standalone readers with their own CMake files. |

## Includes

Files include each other by bare name (`#include "Theme.hh"`) across all of these folders.
`native/host/SourceIncludeDirs.cmake` keeps the list of domain folders in `BTS_HOST_DOMAINS`
and builds `BTS_SOURCE_INCLUDE_DIRS` from it. Every `bts-*` target searches `native/host`,
`native/harness` and each listed domain folder. The standalone projects in
`native/tests/*/CMakeLists.txt` include the same file, so they search the same folders.
`scripts/verify/blender_surface.py` resolves includes the same way. No header name under
`native/` matches a Blender header name, so the wider search path cannot shadow Blender.

## Moving files

Use `python3 scripts/mp.py move <folder> --list <file> [--refs]`. It runs `git mv` and rewrites
the `${BTS_HOST_SOURCE_DIR}/<file>` references in `native/host/CMakeLists.txt` and the
`../../host/<file>` paths in the standalone test projects. With `--refs` it also rewrites exact
`native/host/<file>` paths in scripts and `"../host/<file>"` includes in tests. Add a new domain
folder to `BTS_HOST_DOMAINS` in the same change, then rebuild and run the CPU check. Nothing
here builds the standalone projects, so `mp check`'s `test-cmake-paths` gate checks that every
source path they name exists.

`mp move` refuses a file that a script, test or JSON file names, either by file name or by a
quoted bare stem such as `'NativeMaterialImageVariant'`, because some verifiers build paths as
`'native/host/' + stem + '.cc'`. Pass `--allow-referenced` only after checking each hit.

Before moving a file, check that nothing pins it. Some verifiers in `scripts/host/` and
`native/tests/` recompile a source by name, hash it, or read its text. Moving such a file
needs the verifier updated with it, and some sha256 pins are reviewed provenance that only the
owner can renew. Verifiers that need a host file by name resolve it with `host_rel`,
`host_file` or `host_rel_at` in `scripts/host/main_source.py` instead of writing
`'native/host/' + name`, so the file can move between domain folders.

Source scanners list these folders with `rglob`, and
`scripts/tests/test_recursive_source_listing.py` fails on a flat listing of an owned folder.
