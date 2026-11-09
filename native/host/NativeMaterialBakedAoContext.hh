#pragma once
#include "MeshMaps.hh"
#include "NativeMaterialSourcePolicy.hh"
#include "GpuTextureResource.hh"
#include <map>
#include <span>
#include <string_view>
#include <utility>
namespace bts {
[[noreturn]] inline void native_baked_ao_reject(int tile, const std::string &reason)
{
  throw NativeFragmentContextError(NativeFragmentContextCode::BakedAo,
                                   "Native fragment project AO tile " + std::to_string(tile) +
                                       ": " + reason);
}
inline std::string native_baked_ao_key(const MeshMapPlane &plane, int tile)
{
  if (tile < 1001 || tile > 9999)
    native_baked_ao_reject(tile, "invalid requested UDIM");
  if (!plane.pixels)
    native_baked_ao_reject(tile, "baked pixels are missing");
  if (plane.resolution != plane.pixels->resolution || plane.rows() != plane.pixels->height)
    native_baked_ao_reject(tile, "baked dimensions differ from immutable pixels");
  if (plane.hash.empty() || plane.hash != plane.pixels->hash)
    native_baked_ao_reject(tile, "baked hash differs from immutable pixels");
  return "baked:" + plane.pixels->hash + ":" + std::to_string(plane.pixels->resolution) + ":" +
         std::to_string(plane.pixels->height);
}
class NativeMaterialBakedAoContext {
  bool requested_ = false;
  std::map<int, MeshMapPlane> planes_;

 public:
  static NativeMaterialBakedAoContext capture(const MeshMaps &maps,
                                              std::span<const int> tiles,
                                              bool requested = true)
  {
    NativeMaterialBakedAoContext result;
    if (!requested)
      return result;
    result.requested_ = true;
    for (int tile : tiles) {
      if (!maps[3])
        native_baked_ao_reject(tile,
                               "project Ambient Occlusion map is missing; Bake Mesh Maps first");
      const auto found = maps[3]->tiles.find(tile);
      if (found == maps[3]->tiles.end())
        native_baked_ao_reject(tile, "requested baked tile is missing");
      native_baked_ao_key(found->second, tile);
      result.planes_.emplace(tile, found->second);
    }
    if (result.planes_.empty())
      native_baked_ao_reject(1001, "requested tile list is empty");
    return result;
  }
  bool requested() const
  {
    return requested_;
  }
  const std::map<int, MeshMapPlane> &planes() const
  {
    return planes_;
  }
  const MeshMapPlane &plane(int tile) const
  {
    const auto found = planes_.find(tile);
    if (!requested_ || found == planes_.end())
      native_baked_ao_reject(tile, "immutable requested AO snapshot is unavailable");
    return found->second;
  }
  std::string signature(int tile) const
  {
    return ";project-AO-tile=" + std::to_string(tile) + ":" +
           native_baked_ao_key(plane(tile), tile);
  }
  void validate() const
  {
    if (!requested_ || planes_.empty())
      native_baked_ao_reject(1001, "immutable requested AO snapshot is unavailable");
    for (const auto &[tile, p] : planes_)
      native_baked_ao_key(p, tile);
  }
};
// The real uploader and CPU fixture share this route. Pinning is independent of
// readiness, so a partial upload cannot be evicted between frames. No GPU
// handle/deleter is needed by this state or by its callback-connected CPU tests.
struct NativeBakedAoPin {
  std::string key;
  int width = 0, height = 0;
  std::shared_ptr<const void> residency;
  void validate(const MeshMapPlane &plane, int tile) const
  {
    if (!residency || key != native_baked_ao_key(plane, tile) || width != plane.resolution ||
        height != plane.rows())
      native_baked_ao_reject(tile,
                             "retained upload identity/dimensions are unavailable or changed");
  }
};
class NativeBakedAoLeaseState {
  NativeBakedAoPin pin_;
  bool ready_ = false;

 public:
  const NativeBakedAoPin &pin() const
  {
    return pin_;
  }
  bool ready() const
  {
    return ready_;
  }
  void reset()
  {
    pin_ = {};
    ready_ = false;
  }
  template<class Acquire, class Upload>
  bool prepare(const MeshMapPlane &plane, int tile, const Acquire &acquire, const Upload &upload)
  {
    const auto key = native_baked_ao_key(plane, tile);
    if (!pin_.residency || pin_.key != key || pin_.width != plane.resolution ||
        pin_.height != plane.rows())
    {
      auto candidate = acquire();
      candidate.validate(plane, tile);
      pin_ = std::move(candidate);
      ready_ = false;
    }
    pin_.validate(plane, tile);
    if (!ready_)
      ready_ = upload(pin_);
    return ready_;
  }
  void validate(const MeshMapPlane &plane, int tile, bool require_ready = true) const
  {
    pin_.validate(plane, tile);
    if (require_ready && !ready_)
      native_baked_ao_reject(tile, "retained upload is not complete");
  }
};
struct NativeBakedAoLease {
  GpuTextureResource resource;
  NativeBakedAoLeaseState state;
  void reset()
  {
    resource = {};
    state.reset();
  }
  void validate(const MeshMapPlane &plane, int tile, bool require_ready = true) const
  {
    state.validate(plane, tile, require_ready);
    const auto &pin = state.pin();
    if (!resource || resource.key != pin.key || resource.width != pin.width ||
        resource.height != pin.height || resource.residency != pin.residency)
      native_baked_ao_reject(tile, "GPU allocation no longer matches its retained upload");
  }
};
struct NativeBakedAoInput {
  int tile = 0;
  const MeshMapPlane *plane = nullptr;
  const NativeBakedAoLease *lease = nullptr;
  void validate(int requested_tile) const
  {
    if (tile != requested_tile || !plane || !lease)
      native_baked_ao_reject(requested_tile, "retained draw binding is missing");
    lease->validate(*plane, tile);
  }
};
inline std::string_view native_baked_ao_vertex_assignment()
{
  return "fragment_raster_uv=raster_uv-native_raster_origin;";
}
inline std::string_view native_baked_ao_fragment_source()
{
  return "float native_project_ao_get(){return textureLod(native_project_ao,fragment_raster_uv,0.0).r;}\n";
}
}  // namespace bts
