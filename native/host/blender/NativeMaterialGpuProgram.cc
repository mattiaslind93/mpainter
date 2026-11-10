// SPDX-License-Identifier: GPL-3.0-or-later
#include "BLI_listbase.h"
#include "NativeMaterialGpuProgram.hh"
#include "NativeMaterialGraph.hh"
#include "NativeMaterialSharedGpuImages.hh"
#include "NativeMaterialTiledStorage.hh"
#include "NativeGpuImages.hh"
#include "NativeMaterialRamp.hh"
#include "NativeMaterialSampler.hh"
#include "BKE_colorband.hh"
#include "Performance.hh"
#include "MEM_guardedalloc.h"
#include <map>
#include <set>
#include "BKE_image.hh"
#include "BKE_image_gpu_override.hh"
#include "BLI_hash.h"
#include "DNA_userdef_types.h"
#include "GPU_material.hh"
#include "GPU_shader.hh"
#include "GPU_storage_buffer.hh"
#include "GPU_uniform_buffer.hh"
#include "GPU_compute.hh"
#include "GPU_capabilities.hh"
#include "GPU_state.hh"
#include <QScopeGuard>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
namespace bts {
using namespace blender;
struct NativeMaterialGpuProgram::Impl {
  std::shared_ptr<NativeMaterialGraph> source;
  std::set<Image *> retained_tiled;
  GPUMaterial *material = nullptr;
  gpu::StorageBuf *parameters = nullptr, *objects = nullptr, *projection = nullptr;
  NativeMaterialProjection::Rows projection_values{};
  std::vector<NativeMaterialObjectTable::Row> object_values;
  int object_count = 0;
  std::uint64_t object_hash = 0, object_uploads = 0;
  std::vector<std::array<float, 4>> values;
  std::map<int, QJsonObject> ramps;
  std::size_t count = 0;
  int channel = 0;
  bool surface_required = false, ao_required = false, mapping_required = false;
  ~Impl()
  {
    if (projection)
      GPU_storagebuf_free(projection);
    if (objects)
      GPU_storagebuf_free(objects);
    if (parameters)
      GPU_storagebuf_free(parameters);
    if (material)
      GPU_material_free_single(material);
  }
};
NativeMaterialGpuProgram::NativeMaterialGpuProgram(bNodeTree &tree,
                                                   std::size_t count,
                                                   int channel,
                                                   std::shared_ptr<NativeMaterialGraph> source)
    : impl_(std::make_unique<Impl>())
{
  auto &s = *impl_;
  s.source = std::move(source);
  if (s.source)
    for (const auto &entry : s.source->tiled_image_resources)
      if (entry.first && entry.second)
        s.retained_tiled.insert(entry.first);
  s.count = count;
  s.channel = channel;
  NativeMaterialCompilation layout{&tree, {}, count};
  layout.surface_context = true;
  s.material = compile_native_material(layout);
  if (!layout.error.empty())
    throw std::runtime_error(layout.error);
  s.surface_required = layout.surface_required;
  s.mapping_required = layout.mapping_required;
  if (!s.material || GPU_material_status(s.material) != GPU_MAT_SUCCESS)
    throw std::runtime_error("Native material GPU compilation failed");
  for (const auto &attribute : GPU_material_attributes(s.material)) {
    const std::string_view name(attribute.name);
    if (name == "BTS_Mesh_AO")
      s.ao_required = true;
    if (name.starts_with("BTS_Geometry_") || name == "BTS_Coord_Object" ||
        name == "BTS_Coord_Generated" || name == "BTS_Coord_Normal" ||
        name == "BTS_Object_DisplacementScale")
      s.surface_required = true;
  }
  const auto rows = std::max<std::uint64_t>(
                 1, native_mapping_parameter_rows(count, s.mapping_required)),
             bytes = resource_product(rows, sizeof(s.values[0]));
  if (bytes > GPU_max_storage_buffer_size())
    throw std::length_error("Native material inputs exceed driver SSBO block storage");
  s.values.resize(rows);
  s.parameters = GPU_storagebuf_create_ex(s.values.size() * sizeof(s.values[0]),
                                          s.values.data(),
                                          GPU_USAGE_DYNAMIC,
                                          "mPainter native inputs");
  if (!s.parameters)
    throw std::runtime_error("Native material parameter allocation failed");
  const auto projection_bytes = sizeof(s.projection_values);
  if (projection_bytes > GPU_max_storage_buffer_size())
    throw std::length_error("Native Fill projection exceeds driver SSBO storage");

  s.projection = GPU_storagebuf_create_ex(projection_bytes,
                                          s.projection_values.data(),
                                          GPU_USAGE_DYNAMIC,
                                          "mPainter native Fill projection");
  if (!s.projection)
    throw std::runtime_error("Native Fill projection allocation failed");
}
NativeMaterialGpuProgram::~NativeMaterialGpuProgram() = default;
void NativeMaterialGpuProgram::validate_images() const
{
  const auto &s = *impl_;
  for (const auto &binding : GPU_material_textures(s.material))
    if (binding.ima && s.retained_tiled.contains(binding.ima))
      retained_native_tiled_gpu_image(s.source,
                                      binding.ima,
                                      binding.tiled_mapping_name[0] ?
                                          NativeTiledRepresentation::ArrayMapping :
                                          NativeTiledRepresentation::FirstTile2D);
}

std::uint64_t NativeMaterialGpuProgram::object_table_uploads() const
{
  return impl_->object_uploads;
}
void NativeMaterialGpuProgram::update_ramps(const NativeMaterialDescription &description)
{
  auto &s = *impl_;
  for (int i = 0; i < GPU_material_hosted_ramp_count(s.material); ++i) {
    int parameter, row;
    GPU_material_hosted_ramp_binding(s.material, i, parameter, row);
    if (parameter < 0 || std::size_t(parameter) >= description.parameters.size())
      throw std::invalid_argument("Native ramp parameter is missing");
    const auto &value = description.parameters[parameter].ramp;
    if (s.ramps.contains(row) && s.ramps.at(row) == value)
      continue;
    auto band = native_material_ramp(value);
    float *table = nullptr;
    int size = 0;
    BKE_colorband_evaluate_table_rgba(&band, &table, &size);
    const auto release = qScopeGuard([&] { MEM_delete(table); });
    auto *texture = GPU_material_hosted_ramp_texture(s.material);
    if (!texture || size != GPU_texture_width(texture))
      throw std::runtime_error("Native ramp lookup allocation mismatch");
    performance::Scope timing("gpu_native_ramp_upload");
    GPU_texture_update_sub(texture, GPU_DATA_FLOAT, table, 0, row, 0, size, 1, 1);
    s.ramps[row] = value;
  }
}
void NativeMaterialGpuProgram::dispatch(gpu::Texture *target,
                                        std::span<const std::array<float, 4>> values,
                                        const NativeMaterialGpuContext &context,
                                        std::array<int, 4> region,
                                        std::span<const NativeMaterialMapping> mappings)
{
  auto &s = *impl_;
  if ((!mappings.empty() && mappings.size() != s.count) || values.size() != s.count || !target ||
      GPU_texture_format(target) != gpu::TextureFormat::SFLOAT_32_32_32_32 ||
      GPU_texture_dimensions(target) != 2 || GPU_texture_is_array(target) || context.tile < 1001 ||
      context.tile > IMA_UDIM_MAX || region[0] < 0 || region[1] < 0 || region[2] <= 0 ||
      region[3] <= 0 || region[0] + region[2] > GPU_texture_width(target) ||
      region[1] + region[3] > GPU_texture_height(target))
    throw std::invalid_argument("Invalid native material GPU dispatch");
  for (const auto &value : values)
    for (float x : value)
      if (!std::isfinite(x))
        throw std::invalid_argument("Non-finite native material parameter");
  for (const auto &mapping : mappings)
    mapping.validate();
  if (context.projection)
    context.projection->validate();
  const bool projected_surface = context.projection && int(context.projection->mode) >= 1 &&
                                 int(context.projection->mode) <= 5;
  if ((s.surface_required || projected_surface) &&
      (!context.surface || context.surface_stride < 8 || context.surface_size[0] <= 0 ||
       context.surface_size[1] <= 0))
    throw std::invalid_argument("Native material requires a prepared surface atlas");
  const auto finite = [](const auto &range) {
    return std::all_of(range.begin(), range.end(), [](float x) { return std::isfinite(x); });
  };
  if (!finite(context.object_to_world) || !finite(context.world_to_object) ||
      !finite(context.bounds_min) || !finite(context.bounds_max) ||
      !finite(context.camera_position) || !finite(context.camera_forward) ||
      !finite(context.color_to_aces) || !finite(context.map_u) || !finite(context.map_v) ||
      !finite(context.object_color) || !std::isfinite(context.object_random))
    throw std::invalid_argument("Non-finite native material context");
  if (context.surface) {
    const auto capacity = std::uint64_t(GPU_texture_width(context.surface)) *
                          GPU_texture_height(context.surface) *
                          GPU_texture_layer_count(context.surface) * 4;
    const auto needed = std::uint64_t(context.surface_size[0]) * context.surface_size[1] *
                        context.surface_stride;
    if (GPU_texture_format(context.surface) != gpu::TextureFormat::UINT_32_32_32_32 ||
        !GPU_texture_is_array(context.surface) || context.surface_stride < 8 ||
        context.surface_size[0] <= 0 || context.surface_size[1] <= 0 || needed > capacity)
      throw std::invalid_argument("Native material surface atlas layout is invalid");
  }
  if (context.objects && context.objects->size() > 0 &&
      (!context.surface || context.surface_stride < 15))
    throw std::invalid_argument("Native object table requires a surface owner atlas");
  if (s.ao_required && !context.ambient_occlusion)
    throw std::invalid_argument(
        "Native Ambient Occlusion requires a baked AO map — Bake Mesh Maps…");
  // Ordinary inputs retain their worker-decoded path. A marked native generated source
  // without a CPU cache is admitted only from its validated cached GPU handles.
  // Never invoke the BKE upload/partial-update getter for that GPU-only branch.
  std::map<std::pair<Image *, bool>, ImageGPUTextures> resident;
  for (const auto &binding : GPU_material_textures(s.material)) {
    auto *image = binding.ima;
    if (!image)
      continue;
    const bool array_binding = bool(binding.tiled_mapping_name[0]);
    if (s.retained_tiled.contains(image)) {
      if (binding.iuser_available &&
          (binding.iuser.scene || binding.iuser.pass || binding.iuser.layer ||
           binding.iuser.multi_index || binding.iuser.view || binding.iuser.multiview_eye ||
           !native_tiled_default_user_tile(binding.iuser.tile)))
        throw std::invalid_argument(
            "Retained native TILED compute binding requires default ImageUser");
      resident.emplace(
          std::make_pair(image, array_binding),
          retained_native_tiled_gpu_image(s.source,
                                          image,
                                          array_binding ? NativeTiledRepresentation::ArrayMapping :
                                                          NativeTiledRepresentation::FirstTile2D));
      continue;
    }
    if (BKE_image_has_loaded_ibuf(image))
      continue;
    const bool tiled = image->source == IMA_SRC_TILED;
    const auto fail = [&](const char *reason) {
      throw std::runtime_error(
          std::string(
              "Native material image is not decoded and lacks compatible resident GPU storage: ") +
          image->id.name + " (" + reason + ")");
    };
    if (!native_gpu_image(image) ||
        (image->source != IMA_SRC_GENERATED && image->source != IMA_SRC_TILED))
      fail("not an owned native generated GPU source");
    if (binding.iuser_available &&
        (binding.iuser.pass != 0 || binding.iuser.layer != 0 || binding.iuser.multi_index != 0))
      fail("nondefault image view/pass");
    if (tiled && !binding.tiled_mapping_name[0])
      fail("tiled input lacks array mapping binding");
    const auto cached = BKE_image_gpu_override_texture(image);
    const auto *texture = cached.texture ? *cached.texture : nullptr;
    if (!texture || GPU_texture_dimensions(texture) != 2 ||
        GPU_texture_is_array(texture) != tiled || GPU_texture_width(texture) <= 0 ||
        GPU_texture_height(texture) <= 0 || GPU_texture_has_integer_format(texture) ||
        GPU_texture_has_depth_format(texture) || GPU_texture_has_stencil_format(texture))
      fail("sampleable texture missing or invalid");
    const auto expected_format = image->flag & IMA_HIGH_BITDEPTH ?
                                     gpu::TextureFormat::SFLOAT_32_32_32_32 :
                                     gpu::TextureFormat::SFLOAT_16_16_16_16;
    if (GPU_texture_format(texture) != expected_format)
      fail("native generated precision mismatch");
    if (tiled && (!cached.tile_mapping || !*cached.tile_mapping))
      fail("UDIM mapping missing");
    ImageGpuTileRegion tile;
    if (!BKE_image_gpu_override_tile_region(image, tiled ? context.tile : 1001, tile) ||
        tile.width <= 0 || tile.height <= 0 || tile.x < 0 || tile.y < 0 ||
        tile.x > GPU_texture_width(texture) - tile.width ||
        tile.y > GPU_texture_height(texture) - tile.height || tile.layer < 0 ||
        tile.layer >= GPU_texture_layer_count(texture))
      fail("selected tile outside resident allocation");
    const ImageTile *declared = nullptr;
    for (const auto &candidate : image->tiles)
      if (candidate.tile_number == (tiled ? context.tile : 1001))
        declared = &candidate;
    if (!declared || declared->gen_x <= 0 || declared->gen_y <= 0 ||
        tile.width != declared->gen_x || tile.height != declared->gen_y)
      fail("resident tile differs from declared native dimensions");
    resident.emplace(std::make_pair(image, array_binding), cached);
  }
  bool changed = !std::equal(values.begin(), values.end(), s.values.begin());
  std::copy(values.begin(), values.end(), s.values.begin());
  if (s.mapping_required)
    for (std::size_t i = 0; i < s.count; ++i) {
      const auto rows = (mappings.empty() ? NativeMaterialMapping{} : mappings[i]).rows();
      for (int row = 0; row < 3; ++row) {
        auto &stored = s.values[s.count + 3 * i + row];
        changed |= stored != rows[row];
        stored = rows[row];
      }
    }

  if (changed)
    GPU_storagebuf_update(s.parameters, s.values.data());
  const auto projected = context.projection ? context.projection->encoded() :
                                              NativeMaterialProjection::Rows{};
  if (projected != s.projection_values) {
    s.projection_values = projected;
    GPU_storagebuf_update(s.projection, s.projection_values.data());
  }
  auto *shader = GPU_material_get_shader(s.material);
  const int object_binding = GPU_shader_get_ssbo_binding(shader, "native_objects");
  const int projection_binding = GPU_shader_get_ssbo_binding(shader, "native_projection");
  if (object_binding >= 0) {
    const auto *table = context.objects.get();
    const auto needed = table ? table->rows().size() : std::size_t(0);
    const auto capacity = std::bit_ceil(std::max(std::size_t(1), needed));
    if (!s.objects || s.object_values.size() < capacity) {
      const auto bytes = resource_product(capacity, sizeof(s.object_values[0]));
      if (bytes > GPU_max_storage_buffer_size())
        throw std::length_error("Native object context exceeds driver SSBO block storage");
      if (s.objects)
        GPU_storagebuf_free(s.objects);
      s.object_values.assign(capacity, {});
      s.objects = GPU_storagebuf_create_ex(capacity * sizeof(s.object_values[0]),
                                           s.object_values.data(),
                                           GPU_USAGE_DYNAMIC,
                                           "mPainter native object context");
      if (!s.objects)
        throw std::runtime_error("Native material object context allocation failed");
      s.object_count = -1;
    }
    const int count = table ? table->size() : 0;
    if (needed && (s.object_count != count || s.object_hash != table->hash() ||
                   std::memcmp(s.object_values.data(),
                               table->rows().data(),
                               needed * sizeof(s.object_values[0])) != 0))
    {
      std::copy(table->rows().begin(), table->rows().end(), s.object_values.begin());
      GPU_storagebuf_update(s.objects, s.object_values.data());
      ++s.object_uploads;
      s.object_hash = table->hash();
    }
    s.object_count = count;
  }
  GPU_shader_bind(shader);
  if (object_binding >= 0)
    GPU_storagebuf_bind(s.objects, object_binding);
  if (projection_binding >= 0)
    GPU_storagebuf_bind(s.projection, projection_binding);
  GPU_storagebuf_bind(s.parameters, 0);
  auto *uniform = GPU_material_uniform_buffer_get(s.material);
  if (uniform)
    GPU_uniformbuf_bind(uniform, GPU_shader_get_ubo_binding(shader, "node_tree"));
  const auto unbind = qScopeGuard([&] {
    GPU_texture_image_unbind(target);
    GPU_texture_unbind_all();
    GPU_storagebuf_unbind(s.parameters);
    if (object_binding >= 0)
      GPU_storagebuf_unbind(s.objects);
    if (projection_binding >= 0)
      GPU_storagebuf_unbind(s.projection);
    if (uniform)
      GPU_uniformbuf_unbind(uniform);
    GPU_shader_unbind();
  });
  const auto has = [&](const char *name) { return GPU_shader_get_uniform(shader, name) >= 0; };
  if (has("native_object_count"))
    GPU_shader_uniform_1i(
        shader, "native_object_count", context.objects ? context.objects->size() : 0);
  if (context.surface && GPU_shader_get_sampler_binding(shader, "native_surface") >= 0)
    GPU_texture_bind(context.surface, GPU_shader_get_sampler_binding(shader, "native_surface"));
  if (context.ambient_occlusion && GPU_shader_get_sampler_binding(shader, "native_mesh_ao") >= 0)
    GPU_texture_bind(context.ambient_occlusion,
                     GPU_shader_get_sampler_binding(shader, "native_mesh_ao"));
  if (has("native_has_ao"))
    GPU_shader_uniform_1i(shader, "native_has_ao", bool(context.ambient_occlusion));
  for (auto &texture : GPU_material_textures(s.material)) {
    gpu::Texture *image = nullptr;
    auto sampler_state = texture.sampler_state;
    if (texture.colorband)
      image = *texture.colorband;
    else if (texture.ima) {
      const auto found = resident.find({texture.ima, bool(texture.tiled_mapping_name[0])});
      const auto native = found != resident.end() ?
                              found->second :
                              BKE_image_get_gpu_material_texture(
                                  texture.ima,
                                  texture.iuser_available ? &texture.iuser : nullptr,
                                  texture.tiled_mapping_name[0] != 0);
      image = native.texture ? *native.texture : nullptr;
      sampler_state = native_material_image_sampler(
          sampler_state,
          GPU_anisotropic_filtering_flags(U.anisotropic_filter),
          BKE_image_gpu_mipmaps_suspended(texture.ima));
      if (texture.tiled_mapping_name[0]) {
        if (!native.tile_mapping || !*native.tile_mapping)
          throw std::runtime_error("Native material UDIM mapping unavailable");
        GPU_texture_bind_ex(*native.tile_mapping,
                            sampler_state,
                            GPU_shader_get_sampler_binding(shader, texture.tiled_mapping_name));
      }
    }
    if (!image)
      throw std::runtime_error("Native material GPU source unavailable");
    if (texture.colorband)
      GPU_texture_bind(image, GPU_shader_get_sampler_binding(shader, texture.sampler_name));
    else
      GPU_texture_bind_ex(
          image, sampler_state, GPU_shader_get_sampler_binding(shader, texture.sampler_name));
  }
  if (has("selected_aov"))
    GPU_shader_uniform_1i(shader, "selected_aov", int(BLI_hash_string("BTS_Channel")));
  if (has("scalar_aov"))
    GPU_shader_uniform_1i(shader, "scalar_aov", s.channel != 0);
  const int origin[2]{region[0], region[1]}, size[2]{region[2], region[3]};
  GPU_shader_uniform_2iv(shader, "native_region_origin", origin);
  GPU_shader_uniform_2iv(shader, "native_region_size", size);
  if (has("native_surface_size"))
    GPU_shader_uniform_2iv(shader, "native_surface_size", context.surface_size.data());
  if (has("native_surface_stride"))
    GPU_shader_uniform_1i(
        shader, "native_surface_stride", context.surface ? context.surface_stride : 0);
  const float tile[2]{float((context.tile - 1001) % 10), float((context.tile - 1001) / 10)};
  if (has("native_tile_offset"))
    GPU_shader_uniform_2fv(shader, "native_tile_offset", tile);
  if (has("native_object_to_world"))
    GPU_shader_uniform_mat4(shader,
                            "native_object_to_world",
                            reinterpret_cast<const float (*)[4]>(context.object_to_world.data()));
  if (has("native_world_to_object"))
    GPU_shader_uniform_mat4(shader,
                            "native_world_to_object",
                            reinterpret_cast<const float (*)[4]>(context.world_to_object.data()));
  if (has("native_bounds_min"))
    GPU_shader_uniform_3fv(shader, "native_bounds_min", context.bounds_min.data());
  if (has("native_bounds_max"))
    GPU_shader_uniform_3fv(shader, "native_bounds_max", context.bounds_max.data());
  if (has("native_object_random"))
    GPU_shader_uniform_1f(shader, "native_object_random", context.object_random);
  if (has("native_object_color"))
    GPU_shader_uniform_4fv(shader, "native_object_color", context.object_color.data());
  if (has("native_map_u"))
    GPU_shader_uniform_3fv(shader, "native_map_u", context.map_u.data());
  if (has("native_map_v"))
    GPU_shader_uniform_3fv(shader, "native_map_v", context.map_v.data());
  if (has("native_camera_position"))
    GPU_shader_uniform_3fv(shader, "native_camera_position", context.camera_position.data());
  if (has("native_camera_forward"))
    GPU_shader_uniform_3fv(shader, "native_camera_forward", context.camera_forward.data());
  if (has("native_perspective"))
    GPU_shader_uniform_1i(shader, "native_perspective", context.perspective);
  if (has("native_color_to_aces"))
    GPU_shader_uniform_mat3(shader,
                            "native_color_to_aces",
                            reinterpret_cast<const float (*)[3]>(context.color_to_aces.data()));
  GPU_texture_image_bind(target, 0);
  GPU_compute_dispatch(shader, (region[2] + 7) / 8, (region[3] + 7) / 8, 1);
  GPU_memory_barrier(GPU_BARRIER_TEXTURE_FETCH | GPU_BARRIER_SHADER_IMAGE_ACCESS |
                     GPU_BARRIER_TEXTURE_UPDATE);
}
}  // namespace bts
