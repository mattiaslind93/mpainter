#pragma once
#include "TextureDocument.hh"
#include "ResourceAvailability.hh"
#include "NativeGpuImages.hh"
#include "GPU_context.hh"
namespace bts {
// GPU owner only. A fixed, uniformly disabled Image input is still sampled by
// Blender. Install a tiny zero override before the first material draw, rather
// than permitting stock image_gpu to allocate a full CPU ImBuf fallback.
inline void require_native_world_normal_gpu_context()
{
  if (!blender::GPU_context_active_get())
    throw std::logic_error("Native world Normal images require the native GPU owner context");
}
inline void native_world_normal_placeholder(blender::Image &image)
{
  require_native_world_normal_gpu_context();
  using namespace blender;
  using namespace blender::bke::image::partial_update;
  const bool tiled = image.source == IMA_SRC_TILED;
  const int target = tiled ? TEXTARGET_2D_ARRAY : TEXTARGET_2D;
  int count = 0, last = 1001;
  for (const auto &tile : image.tiles) {
    ++count;
    last = std::max(last, tile.tile_number);
  }
  if (!count)
    throw std::invalid_argument("Native world Normal image has no tiles");
  const auto bytes = resource_sum(resource_product(count, 16),
                                  tiled ? resource_product(last - 1000, 32) : 0);
  BKE_image_free_gputextures(&image);
  image.runtime->gpuflag |= native_generated_gpu_bit;
  image.flag |= IMA_HIGH_BITDEPTH;
  const auto format = gpu::TextureFormat::SFLOAT_32_32_32_32;
  auto *texture =
      tiled ? GPU_texture_create_2d_array("Native world Normal zero",
                                          1,
                                          1,
                                          count,
                                          1,
                                          format,
                                          GPU_TEXTURE_USAGE_GENERAL,
                                          nullptr) :
              GPU_texture_create_2d(
                  "Native world Normal zero", 1, 1, 1, format, GPU_TEXTURE_USAGE_GENERAL, nullptr);
  if (!texture)
    throw std::bad_alloc();
  image.runtime->gputexture[target][0] = texture;
  const float zero[4]{};
  GPU_texture_clear(texture, GPU_DATA_FLOAT, zero);
  GPU_texture_filter_mode(texture, true);
  GPU_texture_mipmap_range(texture, 0, 0);
  if (tiled) {
    const int size = last - 1000;
    std::vector<float> mapping(std::size_t(size) * 8);
    for (int i = 0; i < size; ++i)
      mapping[i * 4] = -1;
    int layer = 0;
    for (auto &tile : image.tiles) {
      tile.runtime.tilearray_layer = layer++;
      tile.runtime.tilearray_offset[0] = tile.runtime.tilearray_offset[1] = 0;
      tile.runtime.tilearray_size[0] = tile.runtime.tilearray_size[1] = 1;
      const int index = tile.tile_number - 1001;
      mapping[index * 4] = tile.runtime.tilearray_layer;
      mapping[(size + index) * 4 + 2] = mapping[(size + index) * 4 + 3] = 1;
    }
    auto *map = GPU_texture_create_1d_array("Native world Normal zero UDIM map",
                                            size,
                                            2,
                                            1,
                                            format,
                                            GPU_TEXTURE_USAGE_SHADER_READ,
                                            mapping.data());
    if (!map) {
      BKE_image_free_gputextures(&image);
      throw std::bad_alloc();
    }
    image.runtime->gputexture[TEXTARGET_TILE_MAPPING][0] = map;
    GPU_texture_mipmap_mode(map, false, false);
  }
  image.runtime->gpu_pass = image.runtime->gpu_layer = image.runtime->gpu_view = 0;
  if (!image.runtime->partial_update_user)
    image.runtime->partial_update_user = BKE_image_partial_update_create(&image);
  BKE_image_gpu_override_updated(&image);
}
inline void native_world_normal_resize(blender::Image &image, const TextureSet &set)
{
  require_native_world_normal_gpu_context();
  using namespace blender;
  BKE_image_free_buffers(&image);
  image.runtime->gpuflag |= native_generated_gpu_bit;
  image.flag |= IMA_HIGH_BITDEPTH;
  const bool tiled = set.tiles != std::set<int>{1001};
  image.source = tiled ? IMA_SRC_TILED : IMA_SRC_GENERATED;
  image.type = tiled ? IMA_TYPE_IMAGE : IMA_TYPE_UV_TEST;
  std::vector<int> removed;
  for (const auto &tile : image.tiles)
    if (!set.tiles.contains(tile.tile_number))
      removed.push_back(tile.tile_number);
  for (int number : removed)
    BKE_image_remove_tile(&image, BKE_image_get_tile(&image, number));
  for (int number : set.tiles) {
    auto *tile = BKE_image_get_tile(&image, number);
    if (!tile)
      tile = BKE_image_add_tile(&image, number, nullptr);
    if (!tile)
      throw std::bad_alloc();
    tile->gen_x = set.width(number);
    tile->gen_y = set.rows(number);
    tile->gen_depth = 32;
    tile->gen_type = IMA_GENTYPE_BLANK;
    tile->gen_flag |= IMA_GEN_FLOAT | IMA_GEN_TILE;
    std::fill_n(tile->gen_color, 4, 0.f);
  }
  native_world_normal_placeholder(image);
}
inline void native_world_normal_full(blender::Image &image)
{
  require_native_world_normal_gpu_context();
  // Explicitly retire the same-format tiny override: generic allocation checks
  // only format, and must not mistake a placeholder for a full-size producer.
  blender::BKE_image_free_gputextures(&image);
  image.runtime->gpuflag |= native_generated_gpu_bit;
  image.flag |= blender::IMA_HIGH_BITDEPTH;
  prepare_native_gpu_image(&image);
}
}  // namespace bts
