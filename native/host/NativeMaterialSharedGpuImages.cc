#include "NativeMaterialSharedGpuImages.hh"
#include "NativeMaterialTiledStorage.hh"
#include "NativeMaterialFragmentNormal.hh"
#include "NativeFragmentImageReadiness.hh"
#include "BKE_image_gpu_override.hh"
#include "NativeMaterialGraph.hh"
#include "NativeMaterialImageResource.hh"
#include "BKE_image.hh"
#include "BLI_listbase.h"
#include "DNA_image_types.h"
#include "GPU_context.hh"
#include "GPU_texture.hh"
#include <algorithm>
#include <map>
#include <vector>
#include <QScopeGuard>
namespace bts {
namespace {
using namespace blender;
struct Owner {
  std::weak_ptr<NativeMaterialGraph> graph;
  Image *image;
};
using Key = std::weak_ptr<NativeMaterialImageResource>;
using Cache = std::map<Key, std::vector<Owner>, std::owner_less<Key>>;
std::map<const void *, Cache> contexts;
std::shared_ptr<NativeMaterialImageResource> resource(
    const std::shared_ptr<NativeMaterialGraph> &graph, Image *image)
{
  for (const auto &binding : graph->image_resources)
    if (binding.first == image)
      return binding.second;
  return {};
}
Cache &cache()
{
  auto *context = GPU_context_active_get();
  if (!context)
    throw std::logic_error("Shared native GPU images require an owner context");
  auto &result = contexts[context];
  std::erase_if(result, [](const auto &entry) { return entry.first.expired(); });
  return result;
}
void add_owner(std::vector<Owner> &owners,
               const std::shared_ptr<NativeMaterialGraph> &graph,
               Image *image)
{
  std::erase_if(owners, [](const auto &owner) { return owner.graph.expired(); });
  for (const auto &owner : owners)
    if (owner.image == image)
      return;
  owners.push_back({graph, image});
}
}  // namespace
bool reuse_native_gpu_image(const std::shared_ptr<NativeMaterialGraph> &graph,
                            blender::Image *image)
{
  using namespace blender;
  const auto source = resource(graph, image);
  if (!source)
    return false;
  auto &owners = cache()[source];
  std::erase_if(owners, [](const auto &owner) { return owner.graph.expired(); });
  for (const auto &owner : owners) {
    auto retained = owner.graph.lock();
    if (!retained || owner.image == image)
      continue;
    if ((owner.image->flag & IMA_HIGH_BITDEPTH) != (image->flag & IMA_HIGH_BITDEPTH) ||
        owner.image->alpha_mode != image->alpha_mode)
      continue;
    ImageUser user{};
    BKE_imageuser_default(&user);
    const auto cached = BKE_image_get_gpu_material_texture_try(owner.image, &user, false);
    if (!cached.texture || !*cached.texture)
      continue;
    auto &target = *image->runtime;
    if (target.gputexture[TEXTARGET_2D][0])
      throw std::logic_error("Cannot replace a live native image cache");
    GPU_texture_ref(*cached.texture);
    target.gputexture[TEXTARGET_2D][0] = *cached.texture;
    target.gpu_pass = 0;
    target.gpu_layer = 0;
    target.gpu_view = 0;
    target.gpuflag |= owner.image->runtime->gpuflag & IMA_GPU_MIPMAP_COMPLETE;
    BKE_image_tag_time(image);
    add_owner(owners, graph, image);
    return true;
  }
  return false;
}
void remember_native_gpu_image(const std::shared_ptr<NativeMaterialGraph> &graph,
                               blender::Image *image)
{
  const auto source = resource(graph, image);
  if (!source)
    return;
  add_owner(cache()[source], graph, image);
}
namespace {
struct TiledOwner {
  std::weak_ptr<NativeMaterialGraph> graph;
  std::weak_ptr<const NativeMaterialImageTileSet> source;
  Image *image = nullptr;
  std::shared_ptr<const NativeTiledStoragePlan> plan;
  gpu::Texture *primary = nullptr, *mapping = nullptr;
};
struct TiledContext {
  NativeTiledOwnerCache<TiledOwner> shared;
  std::map<std::pair<Image *, NativeTiledRepresentation>, TiledOwner> owners;
};
std::map<const void *, TiledContext> tiled_contexts;
void prune_tiled_cache(TiledContext &cache)
{
  const auto alive = [](const TiledOwner &owner) {
    return !owner.graph.expired() && !owner.source.expired();
  };
  cache.shared.prune(alive);
  std::erase_if(cache.owners, [&](const auto &entry) { return !alive(entry.second); });
}
TiledContext &tiled_cache()
{
  auto *context = GPU_context_active_get();
  if (!context)
    throw std::logic_error("Retained tiled images require their GPU owner context");
  return tiled_contexts[context];
}
std::shared_ptr<const NativeMaterialImageTileSet> tiled_resource(
    const std::shared_ptr<NativeMaterialGraph> &graph, Image *image)
{
  if (graph)
    for (const auto &entry : graph->tiled_image_resources)
      if (entry.first == image)
        return entry.second;
  return {};
}
bool tiled_shape_valid(const NativeTiledStoragePlan &plan,
                       gpu::Texture *primary,
                       gpu::Texture *mapping)
{
  if (!primary)
    return false;
  NativeTiledStorageShape shape;
  shape.width = GPU_texture_width(primary);
  shape.height = GPU_texture_height(primary);
  shape.layers = GPU_texture_layer_count(primary);
  shape.dimensions = GPU_texture_dimensions(primary);
  shape.array = GPU_texture_is_array(primary);
  const auto format = GPU_texture_format(primary);
  shape.supported_format = format == gpu::TextureFormat::SFLOAT_32_32_32_32 ||
                           format == gpu::TextureFormat::SFLOAT_16_16_16_16;
  shape.format = format == gpu::TextureFormat::SFLOAT_32_32_32_32 ?
                     NativeTiledFormat::Float32Rgba :
                     NativeTiledFormat::Float16Rgba;
  if (mapping) {
    shape.map_width = GPU_texture_width(mapping);
    shape.map_layers = GPU_texture_layer_count(mapping);
    shape.map_dimensions = GPU_texture_dimensions(mapping);
    shape.map_array = GPU_texture_is_array(mapping);
    shape.map_float32_rgba = GPU_texture_format(mapping) == gpu::TextureFormat::SFLOAT_32_32_32_32;
  }
  return native_tiled_storage_shape_valid(plan, shape);
}
bool tiled_owner_valid(const TiledOwner &owner)
{
  auto graph = owner.graph.lock();
  if (!graph || owner.source.expired() || !owner.image || !owner.plan)
    return false;
  const bool array = owner.plan->representation == NativeTiledRepresentation::ArrayMapping;
  const auto cached = BKE_image_gpu_override_texture(owner.image, array);
  if (!cached.texture || *cached.texture != owner.primary || !owner.primary)
    return false;
  if (array && (!cached.tile_mapping || *cached.tile_mapping != owner.mapping || !owner.mapping))
    return false;
  return tiled_shape_valid(*owner.plan, owner.primary, owner.mapping);
}
const TiledOwner &tiled_owner(const std::shared_ptr<NativeMaterialGraph> &graph,
                              Image *image,
                              NativeTiledRepresentation representation)
{
  auto &cache = tiled_cache();
  const auto found = cache.owners.find({image, representation});
  if (found != cache.owners.end() && found->second.graph.lock() == graph && found->second.plan) {
    const bool array = representation == NativeTiledRepresentation::ArrayMapping;
    const auto cached = BKE_image_gpu_override_texture(image, array);
    if (cached.texture && *cached.texture &&
        (!array || (cached.tile_mapping && *cached.tile_mapping)) &&
        !tiled_shape_valid(
            *found->second.plan, *cached.texture, array ? *cached.tile_mapping : nullptr))
      throw NativeFragmentContextError(
          NativeFragmentContextCode::Images,
          "Native retained TILED storage dimensions/precision differ from its immutable layout");
    if (tiled_owner_valid(found->second))
      return found->second;
  }
  throw NativeFragmentImagePending(
      "Native retained TILED image upload/cache recovery is pending; target is unchanged");
}
}  // namespace
void publish_native_tiled_gpu_image(const std::shared_ptr<NativeMaterialGraph> &graph,
                                    Image *image,
                                    const std::shared_ptr<const NativeTiledStoragePlan> &plan,
                                    gpu::Texture *&primary,
                                    gpu::Texture *&mapping)
{
  const auto source = tiled_resource(graph, image);
  if (!source || !plan || !primary || image->source != IMA_SRC_TILED)
    throw std::invalid_argument("Native tiled publish lacks the exact retained source/texture");
  if (!native_tiled_storage_source_valid(*image, *source, *plan))
    throw std::invalid_argument("Native tiled source/layout changed before publication");
  const bool array = plan->representation == NativeTiledRepresentation::ArrayMapping;
  if (array && !mapping)
    throw std::invalid_argument("Native tiled array/map pair is incomplete");
  if (!tiled_shape_valid(*plan, primary, mapping))
    throw NativeFragmentContextError(
        NativeFragmentContextCode::Images,
        "Native tiled publish storage differs from the immutable plan");
  struct Runtime {
    ImageTile *tile;
    ImageTile_Runtime value;
  };
  std::vector<Runtime> descriptors;
  if (array) {
    descriptors.reserve(plan->regions.size());
    for (const auto &region : plan->regions) {
      ImageTile *target = nullptr;
      for (auto &tile : image->tiles)
        if (tile.tile_number == region.tile)
          target = &tile;
      if (!target)
        throw std::invalid_argument("Native tiled target table changed before publication");
      auto runtime = target->runtime;
      runtime.tilearray_layer = region.layer;
      runtime.tilearray_offset[0] = region.x;
      runtime.tilearray_offset[1] = region.y;
      runtime.tilearray_size[0] = region.width;
      runtime.tilearray_size[1] = region.height;
      descriptors.push_back({target, runtime});
    }
  }
  auto &cache = tiled_cache();
  prune_tiled_cache(cache);
  TiledOwner record{graph, source, image, plan, primary, mapping};
  // All allocations/validation precede this same-thread nonthrowing commit.
  cache.shared.remember(
      plan->key, record, [](const auto &a, const auto &b) { return a.image == b.image; });
  cache.owners[{image, plan->representation}] = record;
  auto &runtime = *image->runtime;
  const int target = array ? TEXTARGET_2D_ARRAY : TEXTARGET_2D;
  auto *old = runtime.gputexture[target][0];
  auto *old_map = array ? runtime.gputexture[TEXTARGET_TILE_MAPPING][0] : nullptr;
  runtime.gputexture[target][0] = std::exchange(primary, nullptr);
  if (array)
    runtime.gputexture[TEXTARGET_TILE_MAPPING][0] = std::exchange(mapping, nullptr);
  for (const auto &entry : descriptors)
    entry.tile->runtime = entry.value;
  runtime.gpu_pass = 0;
  runtime.gpu_layer = 0;
  runtime.gpu_view = 0;
  runtime.gpuflag |= IMA_GPU_MIPMAP_COMPLETE;
  BKE_image_tag_time(image);
  if (old)
    GPU_texture_free(old);
  if (old_map)
    GPU_texture_free(old_map);
}
bool reuse_native_tiled_gpu_image(const std::shared_ptr<NativeMaterialGraph> &graph,
                                  Image *image,
                                  const std::shared_ptr<const NativeTiledStoragePlan> &plan)
{
  if (!tiled_resource(graph, image) || !plan)
    return false;
  auto &cache = tiled_cache();
  prune_tiled_cache(cache);
  const auto *found = cache.shared.find(plan->key, tiled_owner_valid);
  if (!found)
    return false;
  const auto owner = *found;
  auto retained = owner.graph.lock();
  if (!retained)
    return false;
  auto *primary = owner.primary, *mapping = owner.mapping;
  GPU_texture_ref(primary);
  if (mapping)
    GPU_texture_ref(mapping);
  const auto release = qScopeGuard([&] {
    if (primary)
      GPU_texture_free(primary);
    if (mapping)
      GPU_texture_free(mapping);
  });
  publish_native_tiled_gpu_image(graph, image, plan, primary, mapping);
  return true;
}
ImageGPUTextures retained_native_tiled_gpu_image(const std::shared_ptr<NativeMaterialGraph> &graph,
                                                 Image *image,
                                                 NativeTiledRepresentation representation)
{
  const auto &owner = tiled_owner(graph, image, representation);
  return BKE_image_gpu_override_texture(
      image, owner.plan->representation == NativeTiledRepresentation::ArrayMapping);
}
const NativeTiledStoragePlan &retained_native_tiled_gpu_plan(
    const std::shared_ptr<NativeMaterialGraph> &graph,
    Image *image,
    NativeTiledRepresentation representation)
{
  return *tiled_owner(graph, image, representation).plan;
}

}  // namespace bts
