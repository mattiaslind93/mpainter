# native/tests

Unit tests for the host. Each test is a small program with a `main()` that checks behaviour with plain `assert()`. There is no test framework.

## Add a test

1. Create `native/tests/<name>_test.cc`.
2. In `native/host/CMakeLists.txt`, add a target named `bts-<name>-tests` that compiles the test and the host files it needs, for example:

   ```cmake
   add_executable(bts-trackball-tests ${BTS_HOST_SOURCE_DIR}/../tests/trackball_test.cc ${BTS_HOST_SOURCE_DIR}/OrbitCamera.cc)
   ```

3. Give it `cxx_std_20` and the host include directory, like the targets next to it.

Every target whose name matches `bts-*-tests` joins `bts-cpu-tests` automatically and is built with `NDEBUG` undefined, so `assert()` always runs. CMake writes the list of test programs to `bts-cpu-tests-<config>.json` in the build folder. `scripts/host/cpu_test_plan.py` runs exactly that list.

## Run tests

| What | Command |
|---|---|
| Everything on the CPU | `cmake --build <build> --target bts-cpu-check` |
| The same, with options | `python3 scripts/host/verify-cpu.py --build-dir <build> --output <dir>` |
| Focused release checks | `cmake --build <build> --target bts-release-check` |

`ctest` covers only the few tests registered with `add_test`. Use `bts-cpu-check`.

## Test behaviour, not internals

Assert what a user of the code can observe. The assertion message names the step, for example `assert(saved && "project reopens after save")`. Do not add comments that narrate the phases of a test.

## Red tests

- A red test blocks the merge and the release. It is never skipped to get a build out.
- Only Mattias can put a test in quarantine.
- Red on 8 November 2026: `workspace_shutdown_test.cc` line 54 (typing 0.5 into the fill-transform field), on Linux and on Windows under Wine.
