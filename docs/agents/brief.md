# Worker brief template

Coder copies this into `~/mpainter-work/runs/<yyyy-mm-dd>-<topic>/brief.md` and fills every field before it starts a worker. A brief with an empty field is not ready. The worker reads `AGENTS.md` in the repository root and in every folder it edits before it starts.

```markdown
# <topic>

## Goal
<One sentence: what is true when the work is done.>

## Platform and machine
<linux | windows | macos> on <Bazzite | Mac Studio>. Do not edit files that belong only to another platform.

## Base
Worktree: <path>
Branch: <type>/<topic>, created from origin/main at <full SHA>

## May write
- <paths or globs inside the worktree>
- ~/mpainter-work/runs/<this run>/from-worker.md and report.md

## Must not
- Push, commit or force-push to main. Delete branches or tags. Rewrite shared history.
- Tag, publish, upload to Gumroad, run upload_file.py, or read the Gumroad token.
- Use `git add -A` or `git add .`. Name the files.
- Edit Blender sources except through a numbered patch in third_party/blender/patches/.
- Run two heavy steps at once. Take the machine lock with flock for builds, the VM and GPU tests.
- <task-specific limits>

## Proof
Run: <command>
Done when: <the exact output, file or screenshot that shows success>

## Messages
Read to-worker.md before each step and before you report done. Write status, questions and "read up to #<n>" in from-worker.md.

## Report
Write report.md in this run folder, using docs/agents/report.md. Push the branch and open a PR with .github/pull_request_template.md.
```
