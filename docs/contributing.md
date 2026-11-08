# Contributing to mPainter

These rules apply to people and agents alike. Agents also follow `AGENTS.md` and `docs/agents/coder.md`.

## Commits

- Use Conventional Commits: `type(scope): subject`, with no trailing period. Types are `feat`, `fix`, `docs`, `refactor`, `test`, `chore` and `perf`.
- The scope is the area or platform, for example `fix(windows): reset UUIDs with QUuid()` or `refactor(host): move stroke files`.
- Write the subject as a short imperative sentence.
- Keep commits small and in an order that tells the story: remove first, then reshape, then tidy. Each commit must build on its own.
- The body explains why. It does not repeat the subject.
- Add files by name. Never `git add -A` or `git add .`.

## Branches and worktrees

- Work in a git worktree created from current `origin/main`. One agent or one task per worktree.
- Use short-lived branches named `feat/<topic>`, `fix/<topic>`, `refactor/<topic>` or `docs/<topic>`.
- Push the same day. Rebase on `main` before you open a PR.
- Never force-push a shared branch and never push to `main` directly.

## Pull requests

- Open the PR ready for review, using `.github/pull_request_template.md`.
- A reviewer must be able to read the description in a minute.
- `## Verification` names real runs and their results on each platform, with screenshots where they prove a claim.
- Prefer five narrow PRs to one large one. Dependent PRs stack on each other.
- A PR is merged only when the build and the tests pass on Linux, Windows and macOS for its latest commit, and the branch is up to date with `main`.
- Outside contributions and changes to `.github/`, `.claude/`, `AGENTS.md`, `CLAUDE.md`, `scripts/release/` or `docs/agents/` need Mattias's approval.

## Comments

- Keep a comment only for a reason the code cannot show, such as why Windows uses `_IONBF`.
- Replace "do not remove" or "do not change" comments with a check or a test.
- Test code has no comments that narrate its phases. The assertion message names the step.
- Folder context goes in that folder's `AGENTS.md`, not in comments spread through the files.

## Refactoring

- Lock the behaviour first. Run the CPU suite and the smoke tests before and after, and keep the list of compiled source files the same unless the change is meant to alter it.
- Remove dead code before you move or add anything.
- When you replace an API, move every caller and delete the old one in the same change. No parallel paths.
- Let a script do mechanical moves, so a reviewer can run it again.
- Keep a change only if it makes the code easier to read.
