# Live Displacement image-only revision regression

Test-only source; the first pre-edit baseline negative is retained below.
No image-revision geometry failure or production fix has been verified. Parent
source: `e85d51981c8487b534d90974289bca319d75e940`. Existing default,
controlled-Color, native-material and burst actors/runners remain unchanged.

The dedicated main augments only the optional GuiFixDemo call. Compile three
test TUs against the matching full immutable production snapshot: dedicated
main replacing main.o, displacement actor replacing GuiFixDemo.o, and the
optional geometry observer with its **exact Cycles session.cpp flags**. All
production objects/archives and fork bytes stay frozen. The existing integrated
verifier gains only an explicit displacement selector; its production
attestation and frozen-header machinery are reused. Root must review source,
then admit build and one runtime separately; no automatic retry or old-fixture
reuse.

The actor owns a fresh output directory and creates its own localXY
64x32-quad plane, +Z winding, U=0..2/V=0..1 (two UDIMs), and immutable 128x128
raw float4 texture. R is -.25 / +.25 on each half-period, A=1. Only Displacement
is authored in the selected Fill. Displacement+Bump, strength .5, midlevel0,
Uniform subdivision0, L32F and nearest/repeat Fill sampling stay fixed. Global
min/max, presence and bounds do not change. A real visible `fillOffset0` text
edit sends individual synthetic Qt press/release keys for .5, commits through
Return on the actual spin box, then Tab. This moves the spatial pattern; actual
Undo/Redo follow. This is frontend automation, not physical OS input.
Properties are undocked through the existing UI. No library, shader, camera,
generated geometry, export or alternate render-mode workaround is used.

Before Rendering entry, the current EEVEE FBO and known signed role16 GPU
samples are retained, with no CPU ImBuf acquisition. In Rendering each stage
requires expected current role16 samples and actual terminal128/new epoch plus
a subsequent real Paint/frame_done. Two complete GPU and CPU mirror tile files,
hashes, raw geometry, actual counters/state and FBO/workspace images are saved
**before** geometry assertions. No throughput/next-frame/performance claim.

A dedicated opaque displacement observer retains the existing reviewed
completion/control logic plus opt-in arm/copy methods (off until armed).
The original completion observer pair remains byte-exact for historical
controlled/burst admission; no original hash or parent is relabelled. A future
generation/epoch must be armed before entry/edit. At its first
matching terminal callback: release State lock; try Scene mutex without
blocking; copy CPU positions, undisplaced attribute, triangles/UVs and object
transform; unlock Scene; store owned JSON under State. No Qt/GIL/native-owner
calls or deferred Cycles-pointer use. Contention yields unavailable/inconclusive,
not success or forced work. Exceptions enter existing probe failure recording;
original tag_redraw is forwarded once. Existing Session cancel/join precedes
Scene destruction. Observer restoration and owner-context readback clear run
before host shutdown on success, assertion/error and application quit.

There must be one expected 2145-vertex/4096-triangle mesh. Preserve the imported
object rotation: `OBJImportParams` forward-Z/upY defaults (`IO_wavefront_obj.hh`),
`importer_mesh_utils.cc::transform_object` passes (Y,Z,-Z,Y), and
`math_rotation_c.cc::mat3_from_axis_conversion` key0x351 selects LUT row21.
Its column-major matrix has columns (1,0,0),(0,0,1),(0,-1,0); independently
expected Cycles rows are [1,0,0,0],[0,0,-1,0],[0,1,0,0]. The observed matrix must
match within 3e-5. `scene/object.cpp::ObjectManager::apply_static_transforms` excludes
true displacement, so require recorded `geometry.transform_applied=false`.
This orthonormal rotation preserves length: world rotated normal displacement
transformed back to local coordinates gives the same +/-Z oracle.
All undisplaced vertices must match the independently generated plane. At 24
interior plateau vertices (12/tile), original UV/grid plus known pattern gives
Z=±.125, unchanged 3e-5 oracle. All 2145 displaced X/Y components are checked
against independently expected original X/Y with that same 3e-5 displacement
bound; the original undisplaced grid is still required bit-exact. No expected
value is derived from displaced
positions. Original geometry/UV/topology, Displacement settings/bounds,
authoritative GPU handles and Cycles graph/SVM counts must remain unchanged.
Undo/Redo restore exact displaced position arrays and one history gesture.
Screenshots are visual evidence; this unit does not assert exact shaded RGB8
Undo, whose earlier one-LSB negative remains separate and unchanged.

Public entry: `verify-cycles-live.py --integrated-snapshot --displacement-profile
--snapshot <full-snapshot> --output <fresh-build-output> --build-only`; separately
admit `--run-only` with the same arguments. No `--udim`, CPU-only, sanitizer,
other diagnostic selector or old default/controlled parent is accepted. It uses
one actual128 fixture, three test TUs only, no production refresh/patch replay.
Full native/header/archive/SDK/fork-series attestation is required; no fixed
source commit or fixed patch count is used, so a later correctly rebuilt and
reviewed fork series can test the same source actor. Every runtime uses a fresh
private attempt and requires all four named captures and clean shutdown.

Runtime uses the existing normal startup options `--gui-fix-demo <fresh-attempt>` /
`--gui-fix-resolution 128` and explicit environment, private offscreen/XDG/
resource/recovery paths. The passed gui-fix-mesh argument is deliberately unused
by this dedicated actor; `own-two-tile-grid.obj` is created in the attempt.
No production invalidation change is authorized here. If stale geometry is
observed, compare the smallest Cycles-only source-mesh resync to broad depsgraph
GEOMETRY reevaluation; simply re-displacing already-offset vertices would
accumulate offsets. No such fix is implemented in this test unit.


## Retained first baseline and numerical fixture correction

The one original baseline (`attempt-x77fwrk_`, native PID1256386) exited1 and shut
down clean before any live edit: exact displaced-X/Y equality failed at the
initial capture. This does not reproduce an image-revision geometry bug. Raw
geometry, both complete role16 GPU/CPU tile files/hashes, EEVEE and Cycles
frames are retained. Generation1/terminal epoch3/sample128/draw3 and graph7/SVM1
were observed. Maximum transverse delta was 2.50339508e-6; all 24 independent
interior Z errors were already within 3e-5 (max8.94069672e-8).

The pinned kernel explicitly forces smooth displacement normals
(`kernel/geom/shader_data.h:271` and `:204–212`), decoded/interpolated through
`kernel/geom/triangle.h:144–163` and `kernel/geom/attribute.h:156–158,204–207`.
`util/types_normal.h:28–39` encodes exact +Z to dx=dy32768; `:45–53` decodes with
2/65535, giving transverse components approximately 1/65535, not zero.
At strength*height=.125, this analytically contributes approximately
.125/65535=1.9074e-6 before float coordinate rounding. The kernel then subtracts
world-space P, inverse-transforms its direction (`kernel/bake/bake.h:36–42`),
and adds that floating offset to source vertices (`scene/mesh_displace.cpp:120–127`).
Thus bit-exact transverse zero was an invalid fixture invariant. The correction
uses the existing 3e-5 component bound on all 2145 X/Y values, retaining the same 24 Z
bound, exact original grid/triangles/UV, exact Undo/Redo arrays and exact
GPU/CPU bytes. The first negative is not relabelled; no production change,
new sampling setting, new tolerance or rerun is implied by this source update.


## Retained second baseline and input evidence

The corrected-vector cohort (`attempt-bbe_lu_z`, native PID1271894) passed its
initial geometry checks with the same24 Z/all2145 transverse bounds and exact
full GPU/CPU tiles. It then failed before an edited capture because the old
child-line/text+Tab gesture had not committed. The document offset was still0;
actual failed field text/focus were not recorded. Thus it also does not prove
stale geometry. A clean marker was followed by SIGSEGV11: pending spin-box text
was interpreted on QAbstractSpinBox::hideEvent during QDialog/WorkspaceShell
teardown, emitting mapping callbacks. That crash remains a separate production
lifecycle issue; this test-input correction does not fix or exonerate it.

The separate CPU-only actualQt6.8.3 NumericSpinBox probe is retained under
`build/ba-displacement-input-probe` (source SHA256
179ab20688f8cd900c8a7b64c056f4d239c25f6d04b2921c24fc100639ee9b6f).
It accepted the old multi-character text too, so no general parsing bug was
reproduced. Old Tab succeeded with actual focus; explicit per-character
press/release+Return on the spin committed even in an observed inactive case.
Qt's keyboardTracking=false Return path explicitly interprets text; Tab relies
on focusOut. The actor now uses explicit spin Return before Tab and records
text/value/acceptable-input/focus/activation/locale/model/Undo observations for
each step BEFORE its commit assertion. No setters, new tolerance, new geometry
oracles or old-evidence relabelling are introduced. Runtime remains unadmitted
until reviewed production teardown and a matching full parent are available.
