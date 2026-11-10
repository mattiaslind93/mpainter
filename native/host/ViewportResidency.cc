// SPDX-License-Identifier: GPL-3.0-or-later
#include "GPU_host_texture_memory.hh"
#include "ViewportResidency.hh"
#include "NativeGpuImages.hh"
#include "TextureMemory.hh"
#include "TextureResidencyPolicy.hh"
#include "BKE_image.hh"
#include "BKE_image_hosted.hh"
#include "BKE_main.hh"
#include "BKE_image_residency.hh"
#include "BKE_node.hh"
#include "DNA_image_types.h"
#include "DNA_material_types.h"
#include "DNA_node_types.h"
#include "IMB_imbuf_types.hh"
#include "IMB_imbuf.hh"
#include "GPU_capabilities.hh"
#include "GPU_platform.hh"
#include "GPU_texture.hh"
#include "BLI_string.h"
#include "BLI_listbase.h"
#include <QJsonArray>
#include <QJsonDocument>
#include "Preferences.hh"
#include <chrono>
#include <cstdio>
#include <map>
#include <set>
namespace bts {
namespace {
using namespace blender;
// These pixels are authoritative GPU outputs or externally owned immutable
// sources. Residency bookkeeping must never free their published allocations.
bool fixed_full_image(const Image *image)
{
  return native_gpu_image(image) ||
         (image && image->runtime && (image->runtime->gpuflag & IMA_HOSTED_IMMUTABLE_SOURCE));
}
int mip_for(int largest, int cap)
{
  int mip = 0;
  while (largest > cap && largest > 1) {
    largest /= 2;
    ++mip;
  }
  return mip;
}
std::optional<unsigned> texel_bytes(gpu::TextureFormat format)
{
  int component_bytes = 0;
  switch (format) {
    case gpu::TextureFormat::SFLOAT_16:
    case gpu::TextureFormat::SFLOAT_16_16:
    case gpu::TextureFormat::SFLOAT_16_16_16:
    case gpu::TextureFormat::SFLOAT_16_16_16_16:
      component_bytes = 2;
      break;
    case gpu::TextureFormat::SFLOAT_32:
    case gpu::TextureFormat::SFLOAT_32_32:
    case gpu::TextureFormat::SFLOAT_32_32_32:
    case gpu::TextureFormat::SFLOAT_32_32_32_32:
      component_bytes = 4;
      break;
    case gpu::TextureFormat::UNORM_8:
    case gpu::TextureFormat::UNORM_8_8:
    case gpu::TextureFormat::UNORM_8_8_8:
    case gpu::TextureFormat::UNORM_8_8_8_8:
    case gpu::TextureFormat::SRGBA_8_8_8:
      component_bytes = 1;
      break;
    default:
      return {};
  }
  return GPU_texture_component_len(format) * component_bytes;
}
std::optional<std::uint64_t> storage_bytes(gpu::Texture *texture)
{
  if (!texture)
    return 0;
  const auto bpp = texel_bytes(GPU_texture_format(texture));
  if (!bpp)
    return {};
  const int layers = GPU_texture_is_cube(texture) && !GPU_texture_is_array(texture) ?
                         6 :
                         GPU_texture_layer_count(texture);
  return native_texture_storage_bytes(GPU_texture_dimensions(texture),
                                      GPU_texture_width(texture),
                                      GPU_texture_height(texture),
                                      GPU_texture_depth(texture),
                                      layers,
                                      GPU_texture_mip_count(texture),
                                      *bpp);
}

}  // namespace
struct ViewportResidency::Impl {
  struct Entry {
    Image *image = nullptr;
    std::uint32_t material = 0;
    ResidencyRegion region;
    bool bound = true;
    int current_cap = 0, desired_cap = 0, largest = 1, source_width = 1, source_height = 1;
  };
  std::map<unsigned, Entry> entries;
  std::uint64_t frames = 0, changes = 0, available_texture_bytes = 0, image_probe_changes = 0,
                image_probe_frame = 0;
  std::chrono::steady_clock::time_point last_memory_query{};
  QJsonObject state;
  QJsonArray cached_images;
  bool images_probed = false, available_texture_bytes_known = false;
};
ViewportResidency::ViewportResidency() : impl_(std::make_unique<Impl>()) {}
ViewportResidency::~ViewportResidency() = default;
void ViewportResidency::reset()
{
  BKE_image_gpu_clear_residency_limits();
  impl_ = std::make_unique<Impl>();
}
QJsonObject ViewportResidency::snapshot() const
{
  return impl_->state;
}
void ViewportResidency::update(Main &main,
                               std::span<const ResidencyView> views,
                               std::optional<std::uint32_t> selected,
                               const std::function<SceneSnapshot()> &geometry,
                               bool editing)
{
  auto &s = *impl_;
  ++s.frames;
  const auto now = std::chrono::steady_clock::now();
  const bool refresh_memory = texture_memory_refresh_needed.exchange(false);
  if (refresh_memory || s.last_memory_query == std::chrono::steady_clock::time_point{} ||
      now - s.last_memory_query >= std::chrono::seconds(2))
  {
    int total_kb = 0, free_kb = 0;
    if (!GPU_type_matches(GPU_DEVICE_SOFTWARE, GPU_OS_ANY, GPU_DRIVER_ANY) &&
        GPU_mem_stats_supported())
      GPU_mem_stats_get(&total_kb, &free_kb);
    const auto ram = texture_available_ram_bytes(),
               resident = std::uint64_t(s.state["resident_texture_storage_bytes"].toDouble());
    s.available_texture_bytes = available_texture_storage(total_kb, free_kb, ram, resident);
    const bool driver_valid = coherent_vram_report(total_kb, free_kb);
    s.available_texture_bytes_known = driver_valid || ram > 0;
    if (!s.available_texture_bytes_known) {
      s.state["memory_warning"] =
          "VRAM/RAM queries unavailable; GPU allocation remains authoritative";
    }
    else if (!driver_valid)
      s.state["memory_warning"] = "";
    else
      s.state["memory_warning"] = "";
    texture_free_vram = driver_valid ? std::uint64_t(free_kb) * 1024 : 0;
    texture_vram_query_valid = driver_valid;
    ++texture_vram_query_generation;
    texture_gpu_max_size = GPU_max_texture_size();
    s.last_memory_query = now;
    std::printf(
        "BTS_RESIDENCY_VRAM total_kb=%d free_kb=%d available_ram=%llu available_texture_bytes=%llu\n",
        total_kb,
        free_kb,
        (unsigned long long)ram,
        (unsigned long long)s.available_texture_bytes);
  }
  const auto start = std::chrono::steady_clock::now();
  std::set<unsigned> live;
  std::map<unsigned, std::set<unsigned>> owners;
  for (Material &material : main.materials)
    if (material.nodetree)
      for (bNode &node : material.nodetree->nodes) {
        if (!STRPREFIX(node.name, "__BTS_") || !node.id || GS(node.id->name) != ID_IM)
          continue;
        auto *image = reinterpret_cast<Image *>(node.id);
        const auto uid = image->id.session_uid;
        live.insert(uid);
        owners[uid].insert(material.id.session_uid);
        auto &e = s.entries[uid];
        e.image = image;
        e.material = material.id.session_uid;
      }
  if (live.empty())
    return;

  const auto shader_bound = live;
  for (Image &image : main.images)
    if (s.entries.contains(image.id.session_uid) && !live.contains(image.id.session_uid) &&
        !fixed_full_image(&image))
      BKE_image_gpu_set_residency_limit(&image, 0);
  std::erase_if(s.entries, [&](const auto &p) { return !live.contains(p.first); });
  std::vector<ResidencyRegion> regions;
  std::uint64_t mapping_bytes = 0;
  for (auto &[uid, e] : s.entries) {
    e.bound = shader_bound.contains(uid);
    e.region = {};
    e.region.id = uid;
    e.region.bytes_per_texel = 1;
    e.largest = 1;
    int width = 1, height = 1, tiles = 0, max_tile = 1001;
    for (ImageTile &tile : e.image->tiles) {
      if (native_gpu_image(e.image)) {
        width = std::max(width, tile.gen_x);
        height = std::max(height, tile.gen_y);
        e.region.bytes_per_texel = std::max<unsigned>(e.region.bytes_per_texel,
                                                      e.image->flag & IMA_HIGH_BITDEPTH ? 16 : 8);
        ++tiles;
        max_tile = std::max(max_tile, tile.tile_number);
        continue;
      }
      ImageUser user{};
      user.tile = tile.tile_number;
      auto *buffer = BKE_image_acquire_ibuf(e.image, &user, nullptr);
      if (buffer) {
        width = std::max(width, buffer->x);
        height = std::max(height, buffer->y);
        const auto format = IMB_gpu_get_texture_format(
            buffer, bool(e.image->flag & IMA_HIGH_BITDEPTH), true);
        e.region.bytes_per_texel = std::max(e.region.bytes_per_texel,
                                            texel_bytes(format).value_or(16));
        BKE_image_release_ibuf(e.image, buffer, nullptr);
      }
      ++tiles;
      max_tile = std::max(max_tile, tile.tile_number);
    }
    // Native UDIM packing can waste space on mixed aspect ratios. Reserve the
    // conservative one-max-size-layer-per-tile bound, including every mip.
    const int device_max = GPU_max_texture_size();
    if (device_max > 0 && (width > device_max || height > device_max))
      throw std::length_error("Image dimensions exceed the current GPU texture size limit of " +
                              std::to_string(device_max));
    e.source_width = width;
    e.source_height = height;
    e.region.tiles.assign(std::max(1, tiles), {width, height});
    if (e.image->source == IMA_SRC_TILED)
      mapping_bytes = resource_sum(mapping_bytes,
                                   resource_product(std::uint64_t(max_tile - 1000), 32));
    e.largest = std::max(width, height);
    // Storage demand is independent of camera zoom. Let the hardware choose
    // mips at draw time, keeping full detail available whenever it fits.
    bool active = false;
    for (auto owner : owners[uid])
      active |= selected && *selected == owner;
    const bool inspection = STRPREFIX(e.image->id.name + 2, "BTS Authored Normal Inspection") ||
                            STRPREFIX(e.image->id.name + 2, "BTS Mesh Map Inspection") ||
                            STRPREFIX(e.image->id.name + 2, "BTS Mask Inspection");
    e.region.priority = (active ? 4. : 1.) * (inspection ? .125 : 1.);
    e.region.requested_mip = 0;
    regions.push_back(e.region);
  }
  const auto budget = s.available_texture_bytes;
  const auto plan = plan_full_resolution(regions);
  for (const auto &a : plan) {
    auto &e = s.entries.at(unsigned(a.id));
    e.desired_cap = std::max(1, e.largest >> a.mip);
  }
  int evicted = 0, promoted = 0;
  // Allocation changes only for document/budget/selection changes, never zoom.
  // Evict first; the runtime cap is distinct from the GPU sampling mip range.
  for (auto &[uid, e] : s.entries)
    if (!e.current_cap || e.current_cap > e.desired_cap) {
      e.current_cap = e.desired_cap;
      if (!fixed_full_image(e.image) && BKE_image_gpu_set_residency_limit(e.image, e.current_cap))
        ++evicted;
    }
  for (auto &[uid, e] : s.entries)
    if (e.current_cap < e.desired_cap) {
      e.current_cap = e.desired_cap;
      if (!fixed_full_image(e.image) && BKE_image_gpu_set_residency_limit(e.image, e.current_cap))
        ++promoted;
    }
  for (auto &[uid, e] : s.entries)
    BKE_image_gpu_set_sampling_limit(e.image, e.current_cap);

  std::uint64_t reserved = mapping_bytes, source_bytes = 0;
  bool pending = false;
  for (auto &[uid, e] : s.entries) {
    const int mip = mip_for(e.largest, e.current_cap);
    reserved = resource_sum(reserved, residency_bytes(e.region, mip));
    source_bytes = resource_sum(source_bytes, residency_bytes(e.region, 0));
    pending |= e.current_cap != e.desired_cap;
  }
  const bool memory_known = s.available_texture_bytes_known || budget > 0;
  const bool pressure = memory_known && reserved > budget;
  const bool pressure_changed = s.state["memory_pressure"].toBool() != pressure;
  const auto memory_warning = s.state["memory_warning"];
  const double elapsed =
      std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
  s.changes += evicted + promoted;
  s.state = {{"mode", "fixed full texture-set resolution; GPU stack"},
             {"memory_pressure", pressure},
             {"warning", ""},
             {"memory_warning", memory_warning},
             {"available_texture_bytes", qint64(s.available_texture_bytes)},
             {"max_resolution", -1},
             {"settling", false},
             {"editing", editing},
             {"available_bytes", double(budget)},
             {"memory_known", memory_known},
             {"reserved_upperbound_bytes", qint64(reserved)},
             {"source_upperbound_bytes", qint64(source_bytes)},
             {"refining", pending},
             {"frames", qint64(s.frames)},
             {"changes", qint64(s.changes)}};
  if (evicted || promoted || pressure_changed)
    std::printf(
        "BTS_RESIDENCY available=%llu reserved=%llu source=%llu images=%zu evicted=%d refined=%d cpu_ms=%.3f\n",
        (unsigned long long)budget,
        (unsigned long long)reserved,
        (unsigned long long)source_bytes,
        s.entries.size(),
        evicted,
        promoted,
        elapsed);
}
void ViewportResidency::finish_frame()
{
  auto &s = *impl_;
  if (s.state.isEmpty())
    return;
  // Touching every GPU texture each frame stalls the UI thread while VRAM is tight.
  // Caps change only with the residency plan; the dimension check still runs on that
  // change and about once a second.
  const bool probe = !s.images_probed || s.image_probe_changes != s.changes ||
                     s.frames - s.image_probe_frame >= 60;
  if (probe) {
    QJsonArray images;
    std::uint64_t resident = 0;
    bool exact = true;
    for (auto &[uid, e] : s.entries) {
      auto cached = BKE_image_get_gpu_material_texture_try(
          e.image, nullptr, e.image->source == IMA_SRC_TILED);
      auto *texture = cached.texture ? *cached.texture : nullptr;
      auto *mapping = cached.tile_mapping ? *cached.tile_mapping : nullptr;
      const auto bytes = storage_bytes(texture), map_bytes = storage_bytes(mapping);
      exact &= bytes.has_value() && map_bytes.has_value();
      resident = resource_sum(resident, resource_sum(bytes.value_or(0), map_bytes.value_or(0)));
      if (texture && (GPU_texture_width(texture) > e.current_cap ||
                      GPU_texture_height(texture) > e.current_cap ||
                      GPU_texture_layer_count(texture) > int(e.region.tiles.size())))
        throw std::logic_error("Native texture exceeds its residency reservation");
      images.append(QJsonObject{
          {"image", e.image->id.name + 2},
          {"image_uid", qint64(uid)},
          {"material_uid", qint64(e.material)},
          {"shader_bound", e.bound},
          {"source_width", e.source_width},
          {"source_height", e.source_height},
          {"tiles", int(e.region.tiles.size())},
          {"resolution_reason", "texture set resolution"},
          {"cap", e.current_cap},
          {"sampling_cap", BKE_image_gpu_sampling_limit(e.image)},
          {"bytes_per_texel", int(e.region.bytes_per_texel)},
          {"desired_cap", e.desired_cap},
          {"width", texture ? GPU_texture_width(texture) : 0},
          {"height", texture ? GPU_texture_height(texture) : 0},
          {"layers", texture ? GPU_texture_layer_count(texture) : 0},
          {"mips", texture ? GPU_texture_mip_count(texture) : 0},
          {"storage_bytes", bytes ? QJsonValue(qint64(*bytes)) : QJsonValue()},
          {"mapping_bytes", map_bytes ? QJsonValue(qint64(*map_bytes)) : QJsonValue()},
          {"format",
           texture ? GPU_texture_format_name(GPU_texture_format(texture)) : "not allocated"}});
    }
    if (exact && resident > std::uint64_t(s.state["reserved_upperbound_bytes"].toDouble())) {
      std::fprintf(stderr,
                   "BTS_RESIDENCY_ACCOUNTING_MISMATCH resident=%llu reserved=%llu images=%s\n",
                   (unsigned long long)resident,
                   (unsigned long long)s.state["reserved_upperbound_bytes"].toDouble(),
                   QJsonDocument(images).toJson(QJsonDocument::Compact).constData());
      throw std::logic_error("Native texture storage exceeds its format accounting");
    }
    s.state["resident_texture_storage_bytes"] = exact ? QJsonValue(qint64(resident)) :
                                                        QJsonValue();
    s.state["storage_formats_measured"] = exact;
    s.cached_images = images;
    s.images_probed = true;
    s.image_probe_changes = s.changes;
    s.image_probe_frame = s.frames;
  }
  // update() replaces the state object every frame. Put the last probe back so the log keeps image
  // caps.
  s.state["images"] = s.cached_images;
  s.state["texture_accounting_scope"] =
      "Blender-managed logical storage; excludes raw GL transfers, buffers and driver overhead";
  // Full 64-bit storage accounting; upstream's unsigned-int API is a stub.
  const auto total = blender::gpu::host_texture_memory::live_bytes();
  s.state["all_gpu_texture_allocated_bytes"] = total ? QJsonValue(qint64(total)) : QJsonValue();
  if (qEnvironmentVariableIsSet("BTS_RESOLUTION_LOG")) {
    const auto json = QJsonDocument(s.state).toJson(QJsonDocument::Compact);
    std::printf(
        "BTS_VIEWPORT_RESOLUTION frame=%llu %s\n", (unsigned long long)s.frames, json.constData());
  }
}
}  // namespace bts
