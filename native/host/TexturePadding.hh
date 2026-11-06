#pragma once
#include "SceneSnapshot.hh"
#include "SkewSurface.hh"
#include "PaintPixels.hh"
#include "UvTransfer.hh"
#include "FillProjection.hh"
#include "GeometryMask.hh"
#include <set>
#include <memory>
#include <functional>
namespace blender {struct ImBuf;}
namespace bts {
struct GpuMarginPlan;
struct BakeRayOptions {double frontal=.01,rear=.01;bool use_cage=false,relative=true,average_normals=true,ignore_backface=false,directx=false,match_names=false,per_fragment=false;std::string low_marker="_low",high_marker="_high",ignore_marker="_ignorebf";
  // Scalar per tile/local UV. Only distance-based, averaged-normal high-to-low projection.
  std::function<float(int,double,double)> skew_weight;
  std::function<float(int,double,double,const std::array<float,3> &,std::uint32_t)> skew_weight_at;
  std::map<int,PaintPixels::Bounds> regions;
  bool cache_source=false; // Caller owns an immutable source for this padding instance.
};
// Native Blender baking margins, with geometry restricted to one Texture Set.
class TexturePadding {
 public:
  TexturePadding(const SceneSnapshot &,std::uint32_t material_uid);
  static void validate_cage(const SceneSnapshot &low,const SceneSnapshot &cage);
  ~TexturePadding();
  std::shared_ptr<TexturePadding> clone() const;
  static std::uint64_t invocation_count();
  std::vector<unsigned char> uv_coverage(int resolution,int tile,int height=0);
  void apply(blender::ImBuf &,int tile,bool adjacent_faces);
  std::shared_ptr<const GpuMarginPlan> gpu_margin_plan(int width,int height,int tile,bool adjacent_faces);
  const std::map<std::uint32_t,std::string> &mesh_names() const;
  std::vector<SkewSurfaceVector> skew_vectors(int density,const std::set<int> &tiles);
  std::shared_ptr<const GeometryOwnershipTile> mesh_ownership(int width,int tile,int height=0);
  std::shared_ptr<const FillSurfaceAtlas> projection_geometry(int resolution,const std::set<int> &tiles,bool curvature=false,int height=0);
  std::shared_ptr<const FillSurfaceAtlas> project_source(const SceneSnapshot &low,const SceneSnapshot &high,const SceneSnapshot *cage,const BakeRayOptions &,int width,int height,const std::set<int> &tiles,bool curvature,UvTransferMap *transfer=nullptr);
 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
}
