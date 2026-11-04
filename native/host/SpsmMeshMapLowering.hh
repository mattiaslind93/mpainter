#pragma once
#include "SpsmMaterialImport.hh"
namespace bts {
// Explicit destination interpretation, never a cast of SPSM filter/color enums.
// Decodes the destination's premultiplied AO map R, clamps [0,1], replicates
// scene-linear RGB, ignores coverage as an output mask (uncovered texels = 0).
// Matching UV tile, identity UV, native nearest/clamp; no source equivalence.
enum class SpsmMeshMapPolicy { Unknown, NativeAOGeneratorUVIdentityNearest };
struct SpsmMeshMapOptions {
  SpsmMeshMapPolicy policy = SpsmMeshMapPolicy::Unknown;
  std::uint64_t memory_budget_bytes = 0;
};
// Detached, atomic source-validated restoration of the independent AO-alias
// Color single-Fill subset. Requires a compiler-disabled candidate; attempting
// to enhance an already enabled candidate throws. Other independently prepared
// channels/layers are retained. Procedural inputs are never unwrapped. The
// native Generator effect retains a live same-set/per-tile AO reference; native
// preset apply validates every required destination map before changing the
// set.
PreparedSpsm enhance_prepared_spsm_mesh_maps(const PreparedSpsm &,
                                             SpsmMeshMapOptions = {});
} // namespace bts
