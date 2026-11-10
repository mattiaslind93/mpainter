# Harness context

How `main()` will stop calling demo and acceptance code inline, so that the harness can leave
the release build behind `MPAINTER_WITH_HARNESS` (restructure plan, phase 4).

## Where we are

- `native/harness/HarnessOptions.hh` registers the 148 harness command-line options.
  `HarnessDispatch.hh` runs the tests that need no workspace. `MainHarnessInput.inc` holds the
  synthetic input helpers.
- The rest of `main()` still reads harness options 305 times (`parser.isSet`, `parser.value`,
  `parser.values`; product options are read 15 times) and `BTS_*` environment switches 127 times. They sit inline, mixed with product code, and share
  `main()`'s local lambdas and state (`workspace`, `viewport`, `host`, `document`, the callbacks).
- Five verifiers read `main()`'s callbacks as text: `viewport->stroke_*`,
  `callbacks.create_smart_mask=`, `callbacks.create_smart_material=`,
  `smart_mask_thumbnails->request(asset` and `save_document=`/`open_document=`. Those
  statements stay in `main.cc`.

## The interface

```cpp
namespace bts::harness {
class HarnessContext {
 public:
  explicit HarnessContext(const QCommandLineParser &parser);
  // A harness option: one that add_harness_options() registers. Product options
  // (no-splash, project, workspace, environment, autosave-seconds, recovery-dir,
  // library-dir) are never read through here.
  bool is_set(const QString &option) const;
  QString value(const QString &option) const;
};
}
```

- `main()` builds one `HarnessContext harness(parser)` right after `parser.process(app)` and the
  early dispatch. Every read of a harness option goes through `harness`; product options stay on
  `parser`.
- `mp check` gets a `harness-seam` gate: in `main.cc`, `parser.isSet`/`parser.value` take only
  product options, and `harness.is_set`/`harness.value` take only harness options. The two name
  lists come from `main.cc` and `HarnessOptions.hh` themselves.
- Later steps add, one at a time:
  - `env_set(name)`/`env(name)` for the demo `BTS_*` switches. They read the environment live,
    as today, because some runs set a switch at runtime (`qputenv("BTS_BO_PRESET_DEMO", …)`).
  - Hooks, such as `on_workspace_ready(WorkspaceContext &)`. A `WorkspaceContext` is a struct of
    references to the `main()` locals that a demo block uses. It lets a whole demo block move into
    `native/harness/` without changing what it does.

## Ownership

- `main()` owns the `HarnessContext`. It lives on `main()`'s stack for the whole run.
- The context owns nothing of the product. It holds a reference to the parser, and later a
  `WorkspaceContext` of references that are valid only during `main()`.
- A demo that schedules work parents its `QObject`s to the workspace, as the inline code does
  today, so Qt deletes them before the workspace is gone.
- The context has no global state and no singleton.

## How MPAINTER_WITH_HARNESS gates it

- `MPAINTER_WITH_HARNESS` is a CMake option, `ON` by default while the harness is moved, so
  every test and verifier keeps working. A release build sets it `OFF`.
- With `OFF`:
  - `native/harness/*` is not compiled.
  - `main.cc` gets `native/host/app/HarnessDisabled.hh` instead. It has the same functions:
    `add_harness_options()` registers nothing, `run_pre_host_test()` and `run_host_test()`
    return no code, and `HarnessContext::is_set()` is a `constexpr false`.
- Before `OFF` can build, every harness call left in `main()` has to sit behind the context
  (a hook, or an `is_set()` branch that only calls hook functions). Otherwise the release build
  would still need harness symbols. The `harness-seam` gate and a link check of an `OFF` build
  are how that is proven.
- Behaviour must stay identical in the `ON` build. Every step is a rebuild, the CPU check (235
  checks with the known 4 base failures), the Xvfb GUI verifiers against the base baseline, and
  the GPU runs `--gui-fix-demo` and `--headless-viewport-test`, which must exit 0.

## Steps

1. `HarnessContext` with `is_set`/`value`, every harness option read in `main()` routed through
   it, and the `harness-seam` gate. A pure substitution: `unit_verify.py` checks that `main.cc`
   differs from the previous version only by `parser.X("<harness option>")` →
   `harness.X("<harness option>")`.
2. `env_set`/`env` for the harness `BTS_*` switches, which are listed in
   `docs/architecture/bts-variables.md`.
3. `WorkspaceContext` and the first hooks, one demo family per step. Each step moves one block
   verbatim, apart from the reads of `main()` locals, which become `ctx.` reads.
4. `MPAINTER_WITH_HARNESS`, `HarnessDisabled.hh`, and an `OFF` build that configures, compiles
   and links. That build is a new build directory, so it needs the owner's go-ahead.
