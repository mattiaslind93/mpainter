# Worker report template

The worker writes `report.md` in its run folder when it stops, whether the work is done or blocked. Coder reads it next to the diff and the files it names.

```markdown
# <topic>: <DONE | BLOCKED | PARTIAL>

## Result
<One or two sentences, outcome first.>

## Proof
| Check | Command | Result | Evidence |
|---|---|---|---|
| <name> | `<command>` | PASS or FAIL | <log path, screenshot path> |

## Change
Branch <type>/<topic> at <full SHA>. PR <url, or "not opened" and why>.

## Blockers
<The first error, its file and line, and about 30 lines of context. "None" when there are none.>

## Side effects
<Files outside the worktree that were created, moved or overwritten. "None" when there are none.>

## Not tested
<What the proof does not cover, such as real GPU on Windows.>
```
