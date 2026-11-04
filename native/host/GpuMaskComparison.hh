#pragma once
#include "GpuStackPlan.hh"
#include "AnchorReference.hh"
#include "GPU_texture.hh"
#include <array>
#include <functional>
#include <optional>
namespace bts {
struct GpuMaskStage;
struct GpuComparisonInput {
 blender::gpu::Texture *image=nullptr;
 bool enabled=false;
 std::array<float,3> base{};
 std::string key;
 std::map<std::uint64_t,GpuStackPage> pages;
};
struct GpuAnchorInput:GpuComparisonInput {bool mask=false;};
using GpuAnchorResolver=std::function<std::optional<GpuAnchorInput>(const QUuid &,const AnchorReference &)>;
inline void gpu_anchor_dependencies(GpuStackNode &node,const GpuAnchorInput &input,int &slot){node.parameters+=input.key;node.parameters.append(reinterpret_cast<const char *>(&input.enabled),sizeof(input.enabled));node.parameters.append(reinterpret_cast<const char *>(&input.mask),sizeof(input.mask));const auto source=slot++;for(const auto &[id,page]:input.pages)node.pages[(std::uint64_t(source)<<32)|id]=page;}
using GpuComparisonInputs=std::array<GpuComparisonInput,2>;
using GpuComparisonResolver=std::function<std::optional<GpuComparisonInputs>(const GpuMaskStage &)>;
// Versions describe already-computed GPU chunks, never CPU pixel hashes.
inline void gpu_comparison_dependencies(GpuStackNode &node,const GpuComparisonInputs &inputs,int &slot){
 for(const auto &input:inputs){
  node.parameters+=input.key;
  node.parameters.append(reinterpret_cast<const char *>(&input.enabled),sizeof(input.enabled));
  node.parameters.append(reinterpret_cast<const char *>(input.base.data()),sizeof(input.base));
  const auto source=slot++;
  for(const auto &[id,page]:input.pages)node.pages[(std::uint64_t(source)<<32)|id]=page;
 }
}
}
