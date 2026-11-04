#pragma once
#include "NativeMaterialSource.hh"
#include <atomic>
#include <memory>

namespace blender {struct bNodeTree;}
namespace bts {
struct NativeMaterialGraph;
struct PreparedNativeFragmentNormal {
 std::shared_ptr<NativeMaterialGraph> source;
 // Private COLOR AOV containing the actual world-space Normal vector. Neither
 // scalar Height nor .5+.5 encoded tangent Normal. Owned by source->main.
 blender::bNodeTree *tree=nullptr;
};
// Worker/diagnostic extraction only: consumes an exclusively owned detached
// source, binds the existing parameters/ramps/mapping/context on a private copy,
// and preserves the Principled Normal chain for later fragment code generation.
// An unlinked Principled Normal reads actual surface geometry, never its socket
// default. No GPU compilation, image decode, registry/UI/policy/export admission,
// default channel3 capability or production derivative contract is established.
PreparedNativeFragmentNormal prepare_native_fragment_normal(
 DetachedNativeMaterial,const std::atomic_bool &,
 const NativeMaterialDescription *retained_source=nullptr);
// Reuses an already prepared, exclusively worker-owned source Main. Keep the
// sole shared owner here before publishing it to a GPU library. This
// adds only a private AOV; it never rereads or decodes source files. Shared source graphs
// are rejected before mutation; only an unpublished source may be supplied. The caller
// retains the source if an optional Normal chain cannot be prepared.
// Its immutable surface contract is selected at Graph construction. Generic
// SourceAuthored consumers keep source-displacement rejection; explicit Painter
// resources evaluate the preserved branch on their effective undisplaced mesh.
// Optional worker preparation, before the sole Graph owner is published. Uses
// the constructor's already-inlined effective Bump capability: flat/default
// sources remain allocation-free. Failure affects only this private contribution.
void prepare_optional_native_world_normal(const std::shared_ptr<NativeMaterialGraph> &,
 const std::atomic_bool &);
PreparedNativeFragmentNormal prepare_owned_native_fragment_normal(
 const std::shared_ptr<NativeMaterialGraph> &,const std::atomic_bool &);
}
