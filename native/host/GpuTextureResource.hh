#pragma once
#include "FillTexture.hh"
#include <memory>
#include <string>
namespace blender::gpu {class Texture;}
namespace bts {
// Transform-independent identity of the existing decoded source pool. Keep
// interpretation in the key: identical encoded bytes can mean color or data.
inline std::string gpu_texture_resource_key(const FillTexture &source){
 const auto &t=source.tiles?*source.tiles->begin()->second:source;
 return "resource:"+t.storage_identity()+":"+(t.raw?"raw":(!t.input_space.empty()?t.input_space:t.source_space))+":"+std::to_string(t.width)+":"+std::to_string(t.height);
}
// An owner-context lease on an already uploaded pool entry. The token contains
// no GPU object/deleter: releasing a lease cannot free a last GPU ref on a CPU
// worker. The pool remains the allocation owner and accounts for its bytes.
struct GpuTextureResource {
 blender::gpu::Texture *texture=nullptr;
 int width=0,height=0;
 std::string key;
 std::shared_ptr<const void> residency;
 explicit operator bool()const{return texture&&residency;}
};
}
