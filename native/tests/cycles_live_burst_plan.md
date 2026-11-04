# Separate six-second held Color-picker diagnostic — source-only proposal

Profile `held-picker-burst-6s-v1`, requested held duration **6 seconds**.
This is a new cohort; it does not relabel, rerun, overwrite or relax the original
three-second `held-picker-burst-v1` attempt. No production changes, shader
arithmetic changes, CPU composites, render-appearance or performance claims.
No C++ preprocessing, build or GPU execution is authorized by this document.

The fresh source worktree is based on merged `778a979`. It copies the reviewed
three-second diagnostic source, keeping the old worktree and every old output
unchanged. The only production-parent fixture overlay remains its sole friend
line, hash-checked after removal against BE collector `970312e`. The original
72 production integration and controlled5 negative remain separate immutable
parents. Neither prior rendered-ROI failure is called resolved by this probe.

## Why six seconds

The first retained attempt is
`build/ba-live-burst-final/runs/attempt-xaocgo4f`. In its **3.06-second** held
interval, 59 frames carried 58 distinct real picker moves and 59 actual owned
Color GPU write generations, but only two captures and one completed GPU copy
produced a consumed CPU-cycle lower bound of one. Zero held publications and
one distinct CPU sample therefore did **not** satisfy the unchanged minimum of
two consumed CPU cycles: the result is **inconclusive**, not accepted starvation.
The measured first capture-to-next-capture cycle was approximately **1.86 s**.
A separately proposed six-second observation permits more cycles; it does not
lower that minimum or change any oracle. There is no automatic retry.

Original report SHA256:
`47d5b90501f1780bd6bef5a50a25dfd7175ca1afc00c3db13c608f131cdd1453`.
Original log SHA256:
`c6e32ae7a688d8c0bb0d9ca087bcddc171d845251ff53c3ff008a201f88cf6de`.
Original inconclusive summary SHA256:
`c11f825c42082dec88a99ae43bdd02cc6272cf8766ff68f087ae9ddfe6aafbd9`.
Its retained run manifest SHA256:
`e4a6ac574a22d1a703fff7d70dcc4fe75d3d10883c73f70b9728eafbbc22591f`.
The new profile requires these exact four prior artifacts before/after building
and before runtime. It adds them to its own manifest parents and build record.
The original manifest keeps the original source, binary and 72/controlled5
parent identities; it is never regenerated from this six-second source.

## Unchanged input, observations and acceptance gates

The actor includes the exact merged fixture once and immediately stops its
original timer, before event delivery. `CyclesFrameHook{}` is explicit in its
base constructor. Only two host-flags TUs are compiled; the controlled observer
TU and `BTS_BA_CONTROLLED_PROFILE_TEST` are absent. The ordinary frame callback
is forwarded once; actual visible ColorPicker SV MouseMoves are queued after
frame_done, never issued reentrantly during drawing. The held interval now ends
at `elapsed() >= 6000`; JSON explicitly reports requested `held_seconds: 6`.

All minima and pixel oracles are unchanged:

- At least 12 real distinct picker moves and held frames, 8 actual owned Color
  post-wrote_image generations plus 8 upload callback increments (proxy only),
  and **2 consumed CPU cycles** established by completed-copy/next-capture flow.
- Exactly one fixture material binding, stable set/Image identity and GPU
  handles, no producer failures, legacy CPU jobs or paused/error mirror status.
- Four tiny immutable CPU cache samples per frame; no held GPU readback, full
  hash, screenshot, renderer-completion wait, full-buffer copy or disk write.
- Missing/malformed/nonfinite/foreign samples, or changing CPU samples with zero
  publication markers, are contradictory/inconclusive, never starvation.
- Complete final baseline/released/Undo/Redo stages: all four 2048 Color and
  Roughness UDIM tiles GPU==CPU bit-exact, same independent mask/formula oracle
  at **3e-5**, baseline document/tiles restored by Undo, final tiles restored by
  Redo, exactly one Undo, unchanged authoritative handles and graph/SVM counts.
- Only after those gates can zero held publications be classified as starvation;
  fewer cycles or missing evidence stays inconclusive. Preserve the first result.

## Separate immutable collector boundary — build and runtime still held

Proposed fresh output: `build/ba-live-burst-six` (must not already contain a
cohort). Proposed runner uses `--integrated-snapshot --udim --burst-profile`,
`--test-parent-manifest build/ba-integrated-cycles-complete/run-manifest.json`,
`--controlled-parent-manifest build/ba-integrated-cycles-controlled5/run-manifest.json`,
and `--burst-previous-attempt build/ba-live-burst-final/runs/attempt-xaocgo4f`.
The snapshot remains `build/artist-72aaa9b5-frozen`. Use NI19 and at most -j2.
Root source review is required before any C++ preprocessor/compiler or GPU call;
GPU admission is separate and exclusive after AW artist correctness.

The sole collector remains the reviewed merged integrated builder, with explicit
named-profile routing, fixed eight-source overlay, exact five final BE hashes,
sole friend validation and complete production dependency rejection. It freezes
and checks both actual TU dependency closures before compilation, retains all
original production objects/archives, substitutes only the burst main and case,
and copies the independently verified original CPU cache executable unchanged.
No production recompilation, patch replay, original-case duplicate or observer.
All source, module, classifier, runner and this plan hashes are recorded anew;
all frozen source/header/SDK/object/archive identities and parents are checked
before/after build and before runtime/classification. New source hashes cannot
be assigned to the old three-second executable. Existing profile behavior in
the old worktree/artifacts is untouched. No cache CPU rerun is needed.

Pure Python admission tests are synthetic and mock every compiler/link call.
They retain the original profile's wrong overlay/friend/callback, production
mutation, stale helper/classifier/parent, foreign generations and bad sample
negatives, and add rejection of changed original inconclusive evidence. These
checks do not constitute a native build or runtime proof.

## Protocol proposal

See `cycles_live_revision_protocol_plan.md` beside this file for a source-only
proposal. No revision-publication code is implemented. The six-second result
must be reviewed independently before considering any production change.
