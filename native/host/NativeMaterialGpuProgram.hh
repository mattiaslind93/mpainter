// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "NativeMaterialGpuCompiler.hh"
#include "NativeMaterialMapping.hh"
#include "NativeMaterialViewContext.hh"
#include "GPU_texture.hh"
#include "NativeMaterialObjectTable.hh"
#include "NativeMaterialProjection.hh"
#include <array>
#include <memory>
#include <span>
namespace bts {
struct NativeMaterialGpuContext {
 // Borrow the existing packed FillSurfacePoint atlas; no second geometry copy.
 blender::gpu::Texture *surface=nullptr,*ambient_occlusion=nullptr;
 std::array<int,2> surface_size{};
 int surface_stride=0,tile=1001;
 std::array<float,16> object_to_world{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
 std::array<float,16> world_to_object=object_to_world;
 std::array<float,3> bounds_min{},bounds_max{1,1,1}; // Original object-space bounds.
 std::array<float,4> object_color{1,1,1,1};
 std::array<float,3> camera_position{0,0,10},camera_forward{0,0,1};
 bool perspective=false;
 NativeMaterialViewContext view; // Invalid until the actual matrix binder publishes atomically.
 // Native scene-linear RGB to canonical layer ACEScg. Data is never transformed.
 std::array<float,9> color_to_aces{1,0,0,0,1,0,0,0,1};
 std::array<float,3> map_u{1,0,0},map_v{0,1,0};
 float object_random=0;
 std::shared_ptr<const NativeMaterialObjectTable> objects;
 // Null retains the established direct-program/identity coordinate contract.
 std::shared_ptr<const NativeMaterialProjection> projection;
};
struct NativeMaterialDescription;
struct NativeMaterialGraph;
class NativeMaterialGpuProgram {
 public:
 // The tree and source image IDs must outlive this program, on the GPU owner.
 NativeMaterialGpuProgram(blender::bNodeTree &,std::size_t parameters,int channel,std::shared_ptr<NativeMaterialGraph> source={});
 ~NativeMaterialGpuProgram();
 NativeMaterialGpuProgram(const NativeMaterialGpuProgram &)=delete;
 NativeMaterialGpuProgram &operator=(const NativeMaterialGpuProgram &)=delete;
 // Updates a caller-owned RGBA32F target region. No CPU evaluation/readback.
 void update_ramps(const NativeMaterialDescription &);
 void validate_images()const;
 std::uint64_t object_table_uploads()const;
 void dispatch(blender::gpu::Texture *,std::span<const std::array<float,4>>,
               const NativeMaterialGpuContext &,std::array<int,4> region,
               std::span<const NativeMaterialMapping> mappings={});
 private:
 struct Impl;std::unique_ptr<Impl> impl_;
};
}
