# Captured-revision publication — source-only proposal, no implementation

The current first three-second burst result is inconclusive under the retained
minimum of two consumed CPU cycles. It establishes live target writes and zero
held publications for one consumed cycle, not sustained starvation. A separate
six-second diagnostic is proposed; no production correction is yet authorized.

`NativeCyclesImages.cc` currently checks every completed tile against
`entry.wanted == captured.revision`, then publishes `entry.published = wanted`.
`changed()` runs even outside Rendering. A genuinely completed older capture
can be discarded after a newer write; simply removing that equality would be
incorrect because it would advertise uncaptured pixels as the newest revision.

If further evidence warrants a protocol change, the smallest candidate is:

1. Capture an explicit per-image revision and complete immutable target identity
   on the GPU owner. Retain `wanted` as the current request independently.
   Accept only a captured revision newer than `published` and no newer than
   `wanted`; never move published backward or label it with a later wanted.
2. Validate before live mutation: same Main/project reset epoch, Image UID,
   material/role association, exact complete sorted UDIM tile-number set,
   every region offset/layer/extent, allocation identity/format and raw/working
   space interpretation. A tile addition must invalidate the whole captured
   image; checking only old tile rows would admit a partial obsolete image.
3. Bare Texture* comparison cannot prove identity: submit() makes an independent
   copy and does not retain its source. Prevent allocation ABA with a genuine
   allocation epoch or owner-held `GPU_texture_ref` keeper from snapshot until
   owner validation/publication and CPU-batch retirement. Owner observes the
   retirement completion before dropping the corresponding keeper. Keep these
   GPU references in owner-only state,
   never in worker/retirement-owned CPU batches. A plain ref keeps allocation
   identity alive without changing the authoritative handle. Do NOT use
   submit_native/pins_ as an identity shortcut: that save-COW route intentionally
   replaces live texture allocations on subsequent writes, violating this
   path's same-handle contract and potentially adding new copy costs.
4. Keep the captured batch immutable on the CPU worker. Complete all dynamic
   RAM/VRAM admission, vectors, identity/revision lookups and retirement handoff
   before the first live cache mutation. Retain the accepted cache publication
   helper's keeper ownership, partial-failure/reporting and no-post-mutation-
   allocation requirements. On complete success set published to the **captured**
   per-image revision, leave wanted newer, and schedule another capture.
   Publish monotonic complete images/batches; no partial tile generation claims.
5. Preserve first Rendering entry/re-entry freshness separately. Current ready()
   waits for published==wanted while entering; replacing this with merely
   published!=0 could expose stale Painting buffers. A fixed entry-admission
   floor must be acquired only after actual target producers/geometry are ready
   for that entry, keyed by epoch and complete identity. New writes may advance
   wanted without moving that fixed floor forever. This needs a distinct entry
   regression; the held-picker probe begins after initial readiness and cannot
   prove it alone.
6. Identity mismatch/reset/cancel/shutdown must discard safely, release every
   source GPU keeper under the owner context, and retire large CPU buffers off
   owner. New per-revision failures must stay visible and pause/recover according
   to the existing policy, not trigger idle spinning or shader/material retags.
   Preserve Image ID_RECALC_SOURCE selective reload, unchanged authoritative
   handles and graph/SVM compilation counts; no per-write Material SHADING.

Required future proof before merging such a change: unchanged strict tests;
actual held-UI publication progress with at least two consumed cycles; exact
latest postrelease and Undo/Redo tiles; stale allocation/ABA, added/removed UDIM,
Main replacement and interpretation negatives; first/re-entry fresh-floor
regression; failure/retirement lifetime and owner-context cleanup. This proposal
is not a promise of general Blender adoption rollback, no latency threshold is
changed, and it does not explain the independent controlled render-ROI failure.
