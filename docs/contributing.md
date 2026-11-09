# Contributing to mPainter

This page covers how code and commits are written. The order of the restructure work is in the
restructure plan; who may merge and release is in the agent and gate documents. Neither is
repeated here.

## Branches and worktrees

- Work on a short-lived branch from current `main`, in its own worktree. Name the branch
  `feat/<topic>`, `fix/<topic>`, `refactor/<topic>` or `docs/<topic>`.
- Keep platform-specific work on its own branch: Linux and Windows files on one, macOS files
  on another. Shared code lands first.

## Commits

- Use Conventional Commits: `type(scope): subject`, for example
  `fix(export): keep 16-bit PNG channels`.
- Make each commit small enough to land on its own.
- Stage files by name. Never `git add -A` or `git add .`.
- Formatting goes in commits that only format (see below). Do not mix it with other changes.

## Pull requests

The description has these headings: **Why**, **What changed**, **Scope**, **Tradeoffs**,
**Blast Radius** and **Verification**. Verification names the commands that actually ran
and what they printed. A check that was skipped is listed as skipped.

## Code

- The version lives only in `pyproject.toml`. Read it with `python3 scripts/mp.py version`.
- Blender changes only through numbered patches in `third_party/blender/patches/`.
- Production code never includes test or harness headers.
- Write a comment only for a why that the code cannot show.
- Write `QUuid()`, never `QUuid x = {}`. The braces do not compile on Windows.

## Formatting

`.clang-format` is Blender's style (column limit 99), with three options off so that formatting
only changes layout:

- `SortIncludes: false`. In the flat `native/host/` tree, include order can change what compiles.
- `BreakStringLiterals: false`. Splitting a literal changes tokens.
- `SortUsingDeclarations: Never`. Sorting moves using-declarations.

The pinned formatter is clang-format 21.1.8 in the `bts-build` container:

```sh
distrobox enter bts-build -- clang-format --style=file -i native/host/Example.cc
```

A format-only change must not change any token, literal, preprocessor directive or include
order. Two traps came up when the tree was first formatted:

- Spaces inside the arguments of a macro that stringizes them end up in a string. `assert(a==b)`
  and `assert(a == b)` give different failure messages, and `Q_ARG` stringizes its type.
- Some verifier scripts under `scripts/host/` and Python tests under `native/tests/` search
  source files for exact text, for example `if(cpu_paint_evaluation_forbidden)throw`, and some
  pin a source file's sha256. Reformatting such a file breaks the verifier, so update the
  verifier and the source in the same change, and run the Python suites below.

Format-only commits are listed in `.git-blame-ignore-revs`. To have `git blame` skip them:

```sh
git config blame.ignoreRevsFile .git-blame-ignore-revs
```

## Checks

Run these before opening a pull request:

```sh
python3 scripts/mp.py check
python3 -m unittest discover -s scripts/tests -p 'test_*.py'
python3 -m unittest discover -s scripts/verify/tests -p 'test_*.py'
python3 -m unittest discover -s scripts/host -p '*_test.py'
python3 -m unittest discover -s native/tests -p '*_test.py'
```

`native/tests/private_gpu_gui_environment_test.py` only runs inside `bts-build` at nice 19 and
errors at import anywhere else.

`mp check` reports each gate as PASS, FAIL, WARN or UNAVAILABLE. UNAVAILABLE is not a pass.
Some gates fail on `main` today because of known debt that later phases of the plan fix.

## Documentation

Write `docs/` in English. Reports from finished work go to `docs/history/`. Knowledge that
outlives one delivery will go to `docs/features/`, which does not exist yet.
