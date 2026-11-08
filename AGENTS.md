# Agent instructions

Build and packaging steps are in [docs/building.md](docs/building.md). Before changing Windows build, packaging or `QUuid` code, read [Lessons from the verified 0.9.2 build](scripts/cross-windows/README.md#lessons-from-the-verified-092-build).

## Windows regressions to avoid

- Reset a `QUuid` with `id = QUuid()`, never `id = {}`. Windows Qt adds `QUuid(const GUID &)` and `operator=(const GUID &)`, so `= {}` is an ambiguous assignment under clang-cl/MSVC even though Linux and macOS compile it. Declarations such as `QUuid id{}` are fine.
- Keep the gates wired: `scripts/host/check-windows-uuid-resets.py` (in `build.sh`, `verify-cpu.py` and CI), `test-windows-uuid-resets.py` and `test-windows-release-gates.py`. When the checker misses a pattern, extend the checker and its fixtures; never weaken or bypass them.
- `pyproject.toml` is the only version source. Never hardcode a version in package names, scripts or examples. Check the built `mPainter.exe` VERSIONINFO, `source-head.txt`, the ZIP name and checksum, and the About dialog.
- `mPainter.exe` must not import `tbbmalloc_proxy.dll`; it crashes startup (`de6fa19`). Never package Mesa/llvmpipe DLLs such as `opengl32.dll` or `libgallium_wgl.dll`; VM tests use them from a separate test folder only.
- To cross-build a new commit in a prepared workspace, run `snapshot-source.py`, `verify-source.py`, `build.sh` and `package.py`. Do not rerun `prepare-source.sh` over prepared Blender and library directories.

## Evidence and releases

- Wine/Xvfb checks, a Windows VM with software rendering, and real Windows GPU hardware are separate evidence levels; none implies the next. Report the level actually used and every known gap. Never call a release verified from partial evidence.
- Every release follows the [cross-platform release rule](docs/building.md#cross-platform-release-rule): the same intended latest commit, version, features and common assets on Linux, Windows and macOS; a build and function test on each platform; macOS signing, notarization and Mattias's approval on his Mac; then verification of the files Gumroad actually delivers. No platform may be stale or skipped.
