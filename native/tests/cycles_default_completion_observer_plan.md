# Default completion observation candidate v2 — source only

This is a new test cohort, not a retry or repair of the retained strict default
ROI negative. No objects, links, tests or runtime have been produced. No
production/fork/runner/admission code changes are proposed here.

`cycles_live_default_observed_artist_test.cc` copies the existing default
fixture, Color gesture, selected GPU/CPU checks and exact ROI comparisons.
It exports the same test entry for a future explicit test-object replacement.
The new opaque `CyclesDefaultCompletionProbe` copies the reviewed first-reset
wrapper/callback and terminal Python-reference cleanup. Its profile code only
reads RNA; it contains no settings-write path. The original actor and original
controlled/displacement probes stay unchanged.

The callback records actual sample, progress, status, Session generation and
sample-decrease epoch. A requested Color edit/Undo/Redo only sets a future
epoch floor; it does not advance the observed epoch. Adaptive terminal samples
below128 are accepted as observed counts, never as128 samples at every pixel.
Initial and reset epochs require an observed progress<1/non-Done event
before any positive-sample terminal event. Because Progress getters lock
independently, a sample-drop event is never itself accepted as active or
terminal evidence; a later callback must show active progress. A sample drop
carrying stale Done/progress1 remains unproven. An unchanged prior Done event
cannot satisfy the future floor. A generation
change is accepted only at an armed entry; unobserved replacement is an error.

After a matching terminal event, one `viewport->update()` requests a real
Paint. The filter returnsfalse and records its starting epoch; the existing
host frame hook records completion. Capture waits for matching generation and
epoch at both ends. It retains actual defaults (adaptive, jitter, denoise,
seed, frame, view/color and lighting), callback trace and draw timestamps
before the unchanged exact ROI assertion. There is no added no-op reload,
continuous redraw, full-tile GPU readback or CPU image composition.

The report is explicitly `default-observation-only-v2` with a separate final
marker. Existing default/controlled admission is not extended to accept it.
A later reviewed build needs the same main/fixture and a dedicated observer
TU compiled with the exact frozen Cycles `blender/session.cpp` flags; no
production ABI refresh is needed. It is not runnable through existing
profile admission until that narrow source cohort is separately reviewed.

## CPU validation plan — not executed

Compile a small test against the actual-used `CyclesDefaultCompletionEpoch.hh`
only; it needs neither Blender nor Qt/GPU. Exercise these cases:

| Case | Inputs | Required outcome |
| --- | --- | --- |
| Initial stale terminal | New generation, sample96/progress1/Done without active evidence | Epoch1 remains unproven. |
| Initial adaptive terminal | New generation, sample0/progress0/non-Done then96/progress1/Done | Epoch1 terminal; actual sample remains96. |
| Adaptive finish after reset | Establish epoch1 terminal, request floor2, observe0/1/Done (stale), then1/.1/non-Done, then96/1/Done | Drop remains unproven; later active evidence then adaptive finish accepts epoch2. |
| Stale Done on sample drop | Establish epoch1 terminal, observe0/1/Done then96/1/Done | Neither event proves active progress; epoch2 remains unproven. |
| Active evidence on drop only | Establish epoch1 terminal, observe0/0/non-Done then96/1/Done | Drop alone is insufficient; later nonterminal callback is required. |
| Zero-sample terminal | Observe active progress, then0/1/Done | Terminal predicate false for this nonempty fixture. |
| Prior Done | After floor2, repeat128/1/Done without a sample decrease | Still epoch1; capture remains unproven. |
| No observed reset | Same terminal sample before/after a requested edit | Never synthesize another epoch; fail/retain trace rather than declaring completion. |
| Missing terminal | Correct epoch but progress<1 or status not Done | Terminal predicate false. |
| Wrong generation | Old/foreign generation against the armed target | Match false; actor rejects an unexpected newer Session. |
| Old Paint | Current terminal2 but draw epoch1, or another generation | No capture until a real matching Paint completes. |
| Invalid observation | Negative sample, NaN/infinite/out-of-range progress | Observe returnsfalse; callback reports failure and still forwards original redraw once. |

Separately review the opaque callback's unchanged lifetime contract: install
only after the first original reset creates a viewport Session and before its
worker starts; no callback/GIL/Qt calls under the State mutex; forward original
redraw once; restore terminally releases Python refs before host shutdown.
These source checks are not an executed lifetime test.

Uncertainties remain explicit: a missing sample-decrease callback leaves an
epoch unproven; the counter is meaningful only with one active viewport tile
(the observer rejects auto-tiled/background Sessions). Terminal plus a later
draw classifies capture timing, but does not identify a one-LSB difference's
film/display cause or establish deterministic rendering. Historical negatives
and the strict ROI equality/bounds must remain unchanged.

V1 source bytes and SHA manifest are preserved privately at
`build/ba-default-completion-source-review/v1-frozen-source` and
`v1-source-manifest.json`; only one v2 cohort is proposed for the repository.
