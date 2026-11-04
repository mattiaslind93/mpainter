# Restored CPU manifest

The restored CMake CPU group contains 200 native executables; `verify-cpu.py` expands it to 231 explicit checks. All executables built. Linux USD tests required their OpenVDB, OpenImageIO and OpenSubdiv dependency closure, now declared on the four USD test targets only.

First complete run: 223 passed, 8 failed (`logs/parity-full-cpu/results.json`). No full-suite pass is claimed by that initial result.

| Check | Cause and correction |
|---|---|
| Baked-map shelf | The negative-test helper used the invalid asset ID `test`, making valid input fail for the wrong reason. It now uses the existing valid UUID and explicitly accepts a valid payload before testing corruption. Corrected test passed. |
| Flatten layer | A migrated former-refusal test actually flattened two 4096² tiles after admission removal. The 4K planning assertion remains; zero-reported-RAM execution and complete pixel equality use the existing 128² fixture. Reference binary completed in 1.96 seconds. The corrected test passed. |
| Native Cycles revisions | Removed the stale synthetic admission expectation and exercised existing publication paths under zero reported RAM. Corrected test passed. |
| Texture extent | Removed the stale admission rejection; now validates a sparse page at the requested full 16384 extent under zero reported RAM. Corrected test passed. |
| UV diagnostic | The marker assertion expected the former hard-coded pink. It now compares the actual theme Error token. Corrected test passed. |
| Export/MaterialX | Build-tree executable lacked packaged MaterialX library resources. The runner now locates the workspace runtime library and supplies `BTS_MATERIALX_BASE`. Exact export rerun passed with no export-code change. |
| Asset drag/drop CPU entry | Its early return bypassed ordinary host shutdown, allowing the static shelf-thumbnail worker to outlive QApplication. The CPU fixture now explicitly drains the worker before Qt teardown. Initial failure was intermittent: five plain repeats and one GDB repeat passed before this lifecycle correction. No production shutdown behavior changed. |
| Native material CPU inspection | Tests expected rejection of unsupported Mapping/Color source context, rejection of the now-supported Diffuse Normal path, and omission warnings for now-supported Coat inputs. Updated checks verify exact fallback constants, retained original errors and warnings, unchanged source graphs, real geometry-normal output for Diffuse, retained linked/constant Coat input, and zero GPU work. The final complete corrected test passed, including these capabilities and retained unsupported-path validation. |

The runner also persists results atomically after each check and records missing executables/timeouts as failures while continuing; a headless mocked regression verified both failure modes and nonzero final exit. Twenty independent script checks passed, including provenance mutation rejection. Historical Cycles source hashes remain distinguished from current test actors with the precise removed preflight statement admitted explicitly.

Second complete run: 230 passed, one material-fixture check failed (`logs/parity-full-cpu-final/results.json`). Final complete run: **231 passed, zero failed**, overall exit 0 (`logs/parity-full-cpu-complete/results.json`, `logs/parity-full-cpu-complete-run.log`). The tested executable SHA256 is `220d7dd81ace1970418d48aedba598811773d13f5970cd60db0fc5fb86a0990d`. Interactive Cycles identity diagnostics continue separately; this result does not claim those GPU/UI checks passed. All execution used bts-build, private xvfb-run displays, no WAYLAND_DISPLAY, isolated workspace XDG/cache/tmp/recovery paths, and no writes to the reference tree.

After the final disabled-world-sentinel registration fix, targeted native-material UI, native Cycles revisions, and native Cycles publication checks all passed on executable SHA256 `b92130ae25d81b535bcc953702d6ba478ff787573b404b480a5a788c7b98663c` (`logs/parity-cycles-final-cpu/results.json`). The complete 231-check run above remains identified with its own earlier binary; only these three checks were repeated after the narrow final change.
