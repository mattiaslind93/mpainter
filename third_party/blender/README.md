# Blender host integration

The patch is based on upstream Blender v5.2.0, commit
`fbe6228777e7d9afefcd61a413844e790ae75db7`.
It includes all modified tracked files and new native host headers from the
working source used for this release. Blender sources are fetched separately.

Run `python3 scripts/prepare-blender.py ../build/blender` from the repository.
The script shallow-clones the official release, verifies the revision, checks
and applies the patch. It refuses unrelated local modifications. Keep the
patched source when distributing binaries to satisfy source availability.

Numbered follow-up patches beside it are applied in order, each with its own
receipt, so an existing source tree can be extended without discarding its
build cache. A follow-up whose change a platform adaptation has already made
in that tree is recorded rather than reapplied.

Blender and these modifications are GPL-licensed; upstream copyright headers
and license files are retained. The patch provides the CMake host entry point,
GPU/context integration, image lifetime and residency support, rendering and
export fixes. Platform validation is described in `docs/building.md`.
