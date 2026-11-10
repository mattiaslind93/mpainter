# native/harness

Test, demo, proof and witness code for the app (`*Demo`, `*Test`, `*Proof`, `*Witness`, `*Fixture`, ...). It is still compiled into `bts-host`, because `main()` calls it. Phase 4 of the restructure plan takes it out of the release build behind `MPAINTER_WITH_HARNESS`; `docs/architecture/harness-context.md` is the design.

## How main() reaches it

- `HarnessOptions.hh` registers the 148 harness command-line options. Product options stay in `main()`.
- `HarnessDispatch.hh` runs the tests that need no workspace (`run_pre_host_test`, `run_host_test`).
- `HarnessContext.hh` is main()'s one view of the harness command line. Read harness options only through it; `python3 scripts/mp.py check` (gate `harness-seam`) enforces that.
- `SessionDemoFlags.inc`, `SessionDemoSequence.inc`, `SessionCaptureDemos.inc` and `SessionDemoActions.inc` are the demo sections of the Painter session; `native/host/app/WorkspaceSession.inc` includes them in order.
- Other `.inc` files here (proof actions, witnesses) are included by host files inside a function or namespace. Each says where at its top.

## Switches

The `BTS_*` environment switches are listed with where they are read in `docs/architecture/bts-variables.md`. Regenerate it with `python3 scripts/mp.py docs` after a change; `mp check` fails when it is stale.

## Rules

- Production code never includes a file from this folder.
- A demo that needs a companion switch must imply it, as `HarnessDispatch.hh` does for the shaderball and Poly Haven demos, so it does not qFatal on a missing control.
- Some reviewed tests pin the sha256 of files here or in `native/tests/`. Check `python3 scripts/mp.py spdx` (it lists pinned files) before editing one.
