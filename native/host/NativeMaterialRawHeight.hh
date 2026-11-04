#pragma once
#include "NativeMaterialSource.hh"
#include <atomic>
#include <memory>
#include <string>

namespace blender {struct bNodeSocket;struct bNodeTree;}
namespace bts {
struct NativeMaterialGraph;
// Provenance of the authored scalar function, not a Bump-response recipe.
// These literal Bump controls are retained and reported, never applied to H.
struct NativeRawHeightProvenance {
 std::string bump_node,height_node,height_socket;
 float strength=0,distance=0,filter_width=0;
 bool invert=false;
 int bypass_nodes=0;
};
struct NativeRawHeightCandidate {
 bool eligible=false;
 std::string reason;
 NativeRawHeightProvenance provenance;
};
// Read-only inspection of an already native-inlined Normal path. This does not
// alter the existing neutral-only Bump capability or admit arbitrary contexts.
NativeRawHeightCandidate classify_native_raw_height(const blender::bNodeSocket &);
struct PreparedNativeRawHeight {
 std::shared_ptr<NativeMaterialGraph> source;
 blender::bNodeTree *tree=nullptr; // Owned by source->main; ordinary channels unchanged.
 NativeRawHeightProvenance provenance;
};
// Phase-one proof API only. Consumes an exclusively owned detached source on a
// worker, preserving its default source capabilities. No registry, GPU, policy,
// persistent instance or UI activation is performed. Conservatively excludes
// inactive links anywhere in that owned source, including unrelated branches;
// this is not an ordinary native import restriction. Muted nodes still use
// Blender's normal inlining/bypass, rather than being reactivated. The actual
// Painter resource path preserves its immutable surface contract here; this
// never activates Height or prepares a world Normal contribution.
PreparedNativeRawHeight prepare_native_raw_height(DetachedNativeMaterial,const std::atomic_bool &,const NativeMaterialDescription *retained_source=nullptr,NativeWorldNormalSurfaceContract=NativeWorldNormalSurfaceContract::SourceAuthored);
}
