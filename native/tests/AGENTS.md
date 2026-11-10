# native/tests

Unit tests for the host. Each test is a small program with a `main()` that checks behaviour with plain `assert()`. There is no test framework.

## Add a test

1. Create `native/tests/<name>_test.cc`.
2. In `native/host/CMakeLists.txt`, add a target named `bts-<name>-tests` that compiles the test and the host files it needs, for example:

   ```cmake
   add_executable(bts-trackball-tests ${BTS_HOST_SOURCE_DIR}/../tests/trackball_test.cc ${BTS_HOST_SOURCE_DIR}/viewport/OrbitCamera.cc)
   ```

3. Give it `cxx_std_20`, like the targets next to it. Every `bts-*` target gets the host include path (all domain folders and `native/harness`) from `native/host/SourceIncludeDirs.cmake`.

Every target whose name matches `bts-*-tests` joins `bts-cpu-tests` automatically and is built with `NDEBUG` undefined, so `assert()` always runs. CMake writes the list of test programs to `bts-cpu-tests-<config>.json` in the build folder. `scripts/host/cpu_test_plan.py` runs exactly that list.

The folders here (`blend-library/`, `native-export-guard/`, `native_image_ui/`, `scene_formats/`, `spsm_native_lowering/`) are standalone CMake projects. They include `native/host/SourceIncludeDirs.cmake`; `python3 scripts/mp.py check` (gate `test-cmake-paths`) checks that every source they name exists.

## Run tests

| What | Command |
|---|---|
| Everything on the CPU | `cmake --build <build> --target bts-cpu-check` |
| The same, with options | `python3 scripts/host/verify-cpu.py --build-dir <build> --output <dir>` |
| Focused release checks | `cmake --build <build> --target bts-release-check` |

`ctest` covers only the few tests registered with `add_test`. Use `bts-cpu-check`.

## Pinned tests

The live Cycles drivers (`cycles_live_*main.cc` and their tests) compile `native/host/app/main.cc` with a renamed `main()` and a `start_gui_fix_demo()` macro. Verifiers in `scripts/host/` pin their sha256. Do not edit them without renewing the pins, which needs the owner. Their `../host/` includes were repointed when the files they include left the top of `native/host`; the pins were not renewed, so the admission verifiers reject them until the owner re-pins.

## Test behaviour, not internals

Assert what a user of the code can observe. The assertion message names the step, for example `assert(saved && "project reopens after save")`. Do not add comments that narrate the phases of a test.

## Red tests

- A red test blocks the merge and the release. It is never skipped to get a build out.
- Only Mattias can put a test in quarantine.
- Red at base 000a9ee and still red: `bts-workspace-shutdown-ui`, `bts-native-material-ui`, `bts-numeric-locale-tests` and `bts-asset-dragdrop-tests` (CPU suite, 235 checks).
