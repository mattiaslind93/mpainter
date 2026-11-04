#pragma once
#include <QString>
#include <array>
#include <cstddef>
#include <memory>
#include <vector>
namespace bts {
struct TextureSet;struct StudioAsset;class MaterialInspection;
struct MaterialParameterMaskCapture {int stack=0,tile=1001,width=0,height=0;std::size_t index=0;};
// Explicit export only. The Host owns this until GPU/worker retirement is done.
// Cancel only marks a token; normal owner events never destroy/join a future.
class MaterialParameterExport {
 public:
 MaterialParameterExport(const TextureSet &,const std::vector<StudioAsset> &,QString directory,QString working_space,std::array<double,9> scene_linear_to_xyz);
 ~MaterialParameterExport(); // Terminal Host shutdown may join the worker.
 void capture(MaterialInspection &,const TextureSet &); // GPU owner, all-three preflight.
 bool poll(bool current); // GPU owner; only ready futures are consumed.
 void cancel()noexcept;
 private:
 struct Impl;std::unique_ptr<Impl> impl_;
};
}
