# Source layout

How `native/` is organised while the restructure plan's phases 4 and 5 are under way.

| Folder | What lives there |
|---|---|
| `native/host/` | The application, compiled into `bts-host`. Files not yet assigned to a domain stay at the top level. |
| `native/host/<domain>/` | Domain folders: `python/`, `materials/openpbr/`, `baking/`, `gpu/` and more as they move. |
| `native/host/openpbr/` | Vendored OpenPBR resources (Apache-2.0), not our code. |
| `native/harness/` | Test, demo and proof code (`*Demo`, `*Test`, `*Proof`, `*Fixture`, `*Witness`, ...). Still compiled into `bts-host`, because `main()` calls it. |
| `native/tests/` | Test programs and their fixtures. |
| `native/spsm/`, `native/sbsar_metadata/` | Standalone readers with their own CMake files. |

## Includes

Files include each other by bare name (`#include "Theme.hh"`) across all of these folders.
`native/host/CMakeLists.txt` keeps the list of domain folders in `BTS_HOST_DOMAINS`. Every
`bts-*` target searches `native/host`, `native/harness` and each listed domain folder.
`scripts/verify/blender_surface.py` resolves includes the same way. No header name under
`native/` matches a Blender header name, so the wider search path cannot shadow Blender.

## Moving files

Use `python3 scripts/mp.py move <folder> --list <file> [--refs]`. It runs `git mv` and rewrites
the `${BTS_HOST_SOURCE_DIR}/<file>` references in `native/host/CMakeLists.txt`. With `--refs`
it also rewrites exact `native/host/<file>` paths in scripts and `"../host/<file>"` includes in
tests. Add a new domain folder to `BTS_HOST_DOMAINS` in the same change, then rebuild and run
the CPU check.

Before moving a file, check that nothing pins it. Some verifiers in `scripts/host/` and
`native/tests/` recompile a source by name, hash it, or read its text. Moving such a file
needs the verifier updated with it, and some sha256 pins are reviewed provenance that only the
owner can renew. `GuiFixDemo.cc/.hh`, `MeshBaker.cc` and the `Spsm*` files stay at the top
level of `native/host/` for these reasons.

Source scanners list these folders with `rglob`, and
`scripts/tests/test_recursive_source_listing.py` fails on a flat listing of an owned folder.
