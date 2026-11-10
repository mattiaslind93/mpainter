// SPDX-License-Identifier: GPL-3.0-or-later
#include "NativeCyclesImages.hh"
#include "NativeCyclesImageCache.hh"
#include "NativeCyclesPublication.hh"
#include "NativeCyclesRevisions.hh"
#include "BKE_image_hosted.hh"
#include "GpuProjectReadback.hh"
#include "ResourceAvailability.hh"
#include "BKE_main.hh"
#include "BKE_image.hh"
#include "BKE_image_gpu_override.hh"
#include "DNA_image_types.h"
#include "DNA_material_types.h"
#include "DEG_depsgraph.hh"
#include "IMB_imbuf.hh"
#include "IMB_imbuf_types.hh"
#include "IMB_colormanagement.hh"
#include "BLI_string.h"
#include "BLI_listbase.h"
#include <QJsonArray>
#include <QJsonObject>
#include <cmath>
#include <algorithm>
#include <bit>
#include <future>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <mutex>
#include <thread>
namespace bts {
using namespace blender;
namespace {
using Buffer = std::shared_ptr<ImBuf>;
using Identity = NativeCyclesImageIdentity;
using OwnerRef = NativeCyclesOwnerReference<gpu::Texture>;
using Clock = std::chrono::steady_clock;
double milliseconds(Clock::time_point start)
{
  return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}
bool tracing()
{
  return std::getenv("BTS_VERIFY_CYCLES_LIVE") != nullptr;
}
Buffer own(ImBuf *b)
{
  return Buffer(b, [](ImBuf *v) {
    if (v)
      IMB_freeImBuf(v);
  });
}
Image *find_image(Main &main, std::uint32_t uid)
{
  for (Image &image : main.images)
    if (image.id.session_uid == uid)
      return &image;
  return nullptr;
}
Material *find_material(Main &main, std::uint32_t uid)
{
  for (Material &material : main.materials)
    if (material.id.session_uid == uid)
      return &material;
  return nullptr;
}
bool publishable_image(const Image &image)
{
  return image.runtime &&
         (image.runtime->gpuflag & (IMA_HOSTED_GPU_OVERRIDE | IMA_HOSTED_CPU_MIRROR)) &&
         ELEM(image.source, IMA_SRC_GENERATED, IMA_SRC_TILED) &&
         ELEM(image.type, IMA_TYPE_IMAGE, IMA_TYPE_UV_TEST);
}
void release_gpu(gpu::Texture *texture)
{
  GPU_texture_free(texture);
  texture_memory_refresh_needed = true;
}
OwnerRef keep(gpu::Texture *texture)
{
  return {texture, GPU_texture_ref, release_gpu};
}
std::array<std::uint32_t, 9> working_matrix(const Main &main)
{
  std::array<std::uint32_t, 9> result;
  for (int c = 0; c < 3; ++c)
    for (int r = 0; r < 3; ++r)
      result[c * 3 + r] = std::bit_cast<std::uint32_t>(main.colorspace.scene_linear_to_xyz[c][r]);
  return result;
}
NativeCyclesTileIdentity tile_identity(Image &image, ImageTile &tile)
{
  ImageGpuTileRegion region;
  if (!BKE_image_gpu_override_tile_region(&image, tile.tile_number, region))
    throw std::runtime_error("Cycles mirror cannot resolve native UDIM region");
  return {tile.tile_number,
          region.x,
          region.y,
          region.layer,
          region.width,
          region.height,
          tile.gen_x,
          tile.gen_y};
}
bool same_header(Main &main,
                 Image &image,
                 std::uint32_t material,
                 int role,
                 gpu::Texture *texture,
                 const Identity &id)
{
  const bool data = IMB_colormanagement_space_name_is_data(image.colorspace_settings.name);
  return publishable_image(image) && id.main == reinterpret_cast<std::uintptr_t>(&main) &&
         id.image == image.id.session_uid && id.material == material && id.role == role &&
         id.allocation == reinterpret_cast<std::uintptr_t>(texture) &&
         id.format == int(GPU_texture_format(texture)) && id.width == GPU_texture_width(texture) &&
         id.height == GPU_texture_height(texture) &&
         id.layers == (GPU_texture_is_array(texture) ? GPU_texture_layer_count(texture) : 1) &&
         id.mips == GPU_texture_mip_count(texture) && id.source == image.source &&
         id.type == image.type && id.alpha_mode == image.alpha_mode && id.data == data &&
         id.declared_space == image.colorspace_settings.name &&
         id.working_space == main.colorspace.scene_linear_name &&
         id.buffer_space ==
             (data ? image.colorspace_settings.name : main.colorspace.scene_linear_name) &&
         id.working_matrix == working_matrix(main);
}
bool same_live_identity(Main &main,
                        Image &image,
                        std::uint32_t material,
                        int role,
                        gpu::Texture *texture,
                        const Identity &id)
{
  if (!same_header(main, image, material, role, texture, id) ||
      std::size_t(BLI_listbase_count(&image.tiles)) != id.tiles.size())
    return false;
  // The normal native sorted tile list is a linear, allocation-free hot path.
  // An unusual reordered list falls back to a newly sorted descriptor below.
  std::size_t index = 0;
  for (ImageTile &tile : image.tiles)
    if (tile_identity(image, tile) != id.tiles[index++])
      return false;
  return true;
}
std::shared_ptr<const Identity> describe(
    Main &main, Image &image, std::uint32_t material, int role, gpu::Texture *texture)
{
  if (!publishable_image(image))
    throw std::runtime_error(
        "Cycles mirror target is not an authoritative native image: " +
        std::string(image.id.name + 2) + " (source=" + std::to_string(image.source) +
        ", type=" + std::to_string(image.type) +
        ", flags=" + std::to_string(image.runtime ? image.runtime->gpuflag : 0) +
        ", role=" + std::to_string(role) + ")");
  const auto count = std::uint64_t(BLI_listbase_count(&image.tiles));
  const auto header = resource_sum(sizeof(Identity) + 2 * sizeof(void *),
                                   sizeof(image.colorspace_settings.name) +
                                       2 * sizeof(main.colorspace.scene_linear_name));

  const auto format = GPU_texture_format(texture);
  if (format != gpu::TextureFormat::SFLOAT_16_16_16_16 &&
      format != gpu::TextureFormat::SFLOAT_32_32_32_32)
    throw std::runtime_error("Cycles mirror requires native float4 target");
  auto id = std::make_shared<Identity>();
  id->main = reinterpret_cast<std::uintptr_t>(&main);
  id->allocation = reinterpret_cast<std::uintptr_t>(texture);
  id->image = image.id.session_uid;
  id->material = material;
  id->role = role;
  id->format = int(format);
  id->width = GPU_texture_width(texture);
  id->height = GPU_texture_height(texture);
  id->layers = GPU_texture_is_array(texture) ? GPU_texture_layer_count(texture) : 1;
  id->mips = GPU_texture_mip_count(texture);
  id->source = image.source;
  id->type = image.type;
  id->alpha_mode = image.alpha_mode;
  id->data = IMB_colormanagement_space_name_is_data(image.colorspace_settings.name);
  id->declared_space = image.colorspace_settings.name;
  id->working_space = main.colorspace.scene_linear_name;
  id->buffer_space = id->data ? id->declared_space : id->working_space;
  id->working_matrix = working_matrix(main);
  id->tiles.reserve(count);
  for (ImageTile &tile : image.tiles)
    id->tiles.push_back(tile_identity(image, tile));
  std::sort(id->tiles.begin(), id->tiles.end(), [](const auto &a, const auto &b) {
    return a.number < b.number;
  });
  if (!native_cycles_valid_identity(*id)) {
    std::string detail = "Cycles mirror requires a complete native image/tile identity: " +
                         std::string(image.id.name + 2) + " role=" + std::to_string(role) +
                         " gpu=" + std::to_string(id->width) + "x" + std::to_string(id->height) +
                         " layers=" + std::to_string(id->layers) +
                         " mips=" + std::to_string(id->mips) + " space=" + id->buffer_space +
                         " image=" + std::to_string(id->image) +
                         " material=" + std::to_string(id->material);
    for (const auto &tile : id->tiles)
      detail += " tile=" + std::to_string(tile.number) + " region=" + std::to_string(tile.width) +
                "x" + std::to_string(tile.height) +
                " generated=" + std::to_string(tile.generated_width) + "x" +
                std::to_string(tile.generated_height) + " xy=" + std::to_string(tile.x) + "," +
                std::to_string(tile.y) + " layer=" + std::to_string(tile.layer);
    throw std::runtime_error(detail);
  }
  return id;
}
struct CaptureImage {
  std::shared_ptr<const Identity> identity;
  std::uint64_t revision = 0, entry_epoch = 0;
};
struct Tile {
  std::size_t image_index = 0;
  NativeCyclesTileIdentity region;
};
struct CpuBatch {
  std::vector<CaptureImage> captures;
  std::vector<Buffer> buffers;
  std::vector<std::uint32_t> images;
  std::vector<NativeCyclesCacheEntry> entries;
  std::uint64_t bytes = 0;
  bool owner_done = false;  // Protected by Impl::retire_mutex once enqueued.
  ~CpuBatch()
  {
    for (auto &entry : entries)
      if (entry.previous)
        IMB_freeImBuf(entry.previous);
  }
};
}  // namespace
struct NativeCyclesImages::Impl {
  struct Entry {
    NativeCyclesRevision revision;
    std::uint32_t material = 0;
    int role = -1;
    std::shared_ptr<const Identity> identity;
    OwnerRef keeper;
  };
  std::map<std::pair<std::uint32_t, int>, QJsonObject>
      bk_packets;  // Empty unless explicit BK diagnostic is enabled.
  std::map<std::uint32_t, Entry> entries;
  std::uint64_t serial = 0, failed_serial = 0, entry_epoch = 0;
  Main *owner_main = nullptr;
  bool enabled = false, publication_attempted = false;
  std::string failure;
  std::unique_ptr<GpuProjectReadback> readback;
  std::vector<Tile> targets;
  std::vector<CaptureImage> captures;
  // These refs NEVER move to CPU preparation/results/retirement. They also
  // survive entry identity rebinding and the clearing of targets at completion.
  std::vector<OwnerRef> capture_keepers;
  std::future<std::unique_ptr<CpuBatch>> worker;
  std::mutex retire_mutex;
  std::condition_variable retire_changed;
  std::thread retirement;
  std::unique_ptr<CpuBatch> retiring;
  bool stopping = false;
  void ensure_retirement()
  {
    if (retirement.joinable())
      return;
    stopping = false;
    retirement = std::thread([this] {
      for (;;) {
        std::unique_ptr<CpuBatch> batch;
        {
          std::unique_lock lock(retire_mutex);
          retire_changed.wait(lock, [&] { return stopping || retiring; });
          if (!retiring) {
            if (stopping)
              return;
            continue;
          }
          batch = std::move(retiring);
          retire_changed.notify_all();
          retire_changed.wait(lock, [&] { return batch->owner_done; });
        }
        batch.reset();  // CPU buffers only, outside the mutex; never a GPU last-ref.
      }
    });
  }
  bool retirement_available()
  {
    std::lock_guard lock(retire_mutex);
    return !retiring;
  }
  CpuBatch *retire(std::unique_ptr<CpuBatch> batch)
  {
    auto *view = batch.get();
    {
      std::lock_guard lock(retire_mutex);
      if (retiring)
        throw std::logic_error("Cycles retirement queue already occupied");
      retiring = std::move(batch);
    }
    retire_changed.notify_one();
    return view;
  }
  void clear_capture()
  {
    readback.reset();
    targets.clear();
    captures.clear();
    capture_keepers.clear();
  }
  void refresh_required(Main &main)
  {
    for (auto i = entries.begin(); i != entries.end();) {
      auto *image = find_image(main, i->first);
      if (!image || !find_material(main, i->second.material)) {
        i = entries.erase(i);
        continue;
      }
      auto &entry = i->second;
      ++i;
      const auto gpu = BKE_image_gpu_override_texture(image);
      if (!gpu.texture || !*gpu.texture)
        throw std::runtime_error("A required live material image has no GPU storage");
      const bool new_epoch = entry.revision.entry_epoch != entry_epoch;
      if (!new_epoch && entry.identity && entry.keeper.get() == *gpu.texture &&
          same_live_identity(
              main, *image, entry.material, entry.role, *gpu.texture, *entry.identity))
        continue;
      auto identity = describe(main, *image, entry.material, entry.role, *gpu.texture);
      auto keeper = keep(*gpu.texture);
      const bool changed_identity = !entry.identity || *entry.identity != *identity;
      if (new_epoch || changed_identity) {
        const auto revision = native_cycles_next_serial(serial);
        native_cycles_require(entry.revision, entry_epoch, revision, changed_identity);
        serial = revision;
      }
      entry.identity = std::move(identity);
      entry.keeper = std::move(keeper);
    }
  }
  bool ready() const
  {
    if (!enabled)
      return true;
    return std::all_of(entries.begin(), entries.end(), [&](const auto &item) {
      const auto &entry = item.second;
      return entry.identity && entry.keeper.get() &&
             native_cycles_revision_ready(entry.revision, entry_epoch);
    });
  }
  void reset()
  {
    // Close/document replacement may wait; ordinary edits only poll futures.
    if (worker.valid())
      try {
        auto batch = worker.get();
        batch->owner_done = true;
        std::unique_lock lock(retire_mutex);
        retire_changed.wait(lock, [&] { return !retiring; });
        retiring = std::move(batch);
        lock.unlock();
        retire_changed.notify_one();
      }
      catch (...) {
      }
    if (retirement.joinable()) {
      {
        std::lock_guard lock(retire_mutex);
        stopping = true;
      }
      retire_changed.notify_one();
      retirement.join();
    }
    clear_capture();
    entries.clear();
    bk_packets.clear();
    owner_main = nullptr;
    enabled = false;
    failed_serial = 0;
    publication_attempted = false;
    failure.clear();
    // Keep epoch monotonic across reset, including a Main-address reuse.
    if (entry_epoch != std::numeric_limits<std::uint64_t>::max())
      ++entry_epoch;
  }
};
NativeCyclesImages::NativeCyclesImages() : impl_(std::make_unique<Impl>()) {}
NativeCyclesImages::~NativeCyclesImages()
{
  impl_->reset();
}
void NativeCyclesImages::reset()
{
  impl_->reset();
}
bool NativeCyclesImages::active() const
{
  return impl_->enabled;
}
bool NativeCyclesImages::image_ready(std::uint32_t uid) const
{
  const auto &s = *impl_;
  const auto found = s.entries.find(uid);
  if (!s.enabled || found == s.entries.end())
    return false;
  const auto &r = found->second.revision;
  return r.published == r.wanted && native_cycles_revision_ready(r, s.entry_epoch);
}
std::uint64_t NativeCyclesImages::entry_epoch() const
{
  return impl_->entry_epoch;
}
void NativeCyclesImages::changed(Image &image, std::uint32_t material)
{
  changed(image, material, -1);
}
void NativeCyclesImages::changed(Image &image, std::uint32_t material, int role)
{
  image.runtime->gpuflag |= IMA_HOSTED_CPU_MIRROR;
  auto &s = *impl_;
  const auto revision = native_cycles_next_serial(s.serial);
  auto &e = s.entries[image.id.session_uid];
  e.revision.wanted = revision;
  e.material = material;
  e.role = role;
  s.serial = revision;
  s.failure.clear();
}
std::string_view NativeCyclesImages::error() const
{
  const auto &s = *impl_;
  return native_cycles_failure(s.failed_serial, s.serial, s.failure);
}
QJsonObject NativeCyclesImages::diagnostic_publication_packets() const
{
  if (!std::getenv("BTS_BK_FOLDER_MASK") && !std::getenv("BTS_BJ_PROJECTION_UI") &&
      !std::getenv("BTS_BM_ADJUSTMENT_DEMO"))
    throw std::logic_error("BK publication diagnostic is not enabled");
  const auto &s = *impl_;
  QJsonArray packets;
  for (const auto &[key, packet] : s.bk_packets) {
    auto row = packet;
    const auto found = s.entries.find(key.first);
    if (found == s.entries.end()) {
      row["current"] = false;
      packets.append(row);
      continue;
    }
    const auto &revision = found->second.revision;
    row["wanted"] = QString::number(revision.wanted);
    row["published"] = QString::number(revision.published);
    row["floor"] = QString::number(revision.floor);
    row["entry_epoch"] = QString::number(revision.entry_epoch);
    row["current"] = s.enabled && revision.published == revision.wanted &&
                     row["packet_revision"].toString().toULongLong() == revision.published &&
                     row["packet_epoch"].toString().toULongLong() == s.entry_epoch &&
                     native_cycles_revision_ready(revision, s.entry_epoch);
    packets.append(row);
  }
  return {{"scope",
           "Successful native CPU mirror publication packets; cache not independently reopened"},
          {"active", s.enabled},
          {"current_entry_epoch", QString::number(s.entry_epoch)},
          {"packets", packets}};
}
bool NativeCyclesImages::pending() const
{
  const auto &s = *impl_;
  if (s.worker.valid() || s.readback)
    return true;
  if (!s.enabled || s.failed_serial == s.serial)
    return false;
  for (auto &[id, e] : s.entries)
    if (e.revision.published != e.revision.wanted ||
        !native_cycles_revision_ready(e.revision, s.entry_epoch))
      return true;
  return false;
}
bool NativeCyclesImages::update(Main &main, bool active)
try {
  auto &s = *impl_;
  if (s.owner_main != &main || (active && !s.enabled)) {
    s.entry_epoch = native_cycles_next_serial(s.entry_epoch);
    s.owner_main = &main;
    if (active) {
      s.failed_serial = 0;
      s.failure.clear();
    }  // Explicit fresh-entry retry.
  }
  s.enabled = active;
  const bool paused = s.failed_serial && s.failed_serial == s.serial;
  if (active && !paused)
    s.refresh_required(main);  // Existing producer readiness boundary is upstream.
  if (!active)
    for (auto &[id, entry] : s.entries)
      entry.keeper.reset();
  const bool worker = s.worker.valid();
  const bool worker_ready = worker && s.worker.wait_for(std::chrono::seconds(0)) ==
                                          std::future_status::ready;
  const auto action = native_cycles_completion_action(
      paused, worker, worker_ready, worker_ready && s.retirement_available());
  if (action == NativeCyclesCompletionAction::Wait)
    return s.ready();
  if (action == NativeCyclesCompletionAction::Paused) {
    s.clear_capture();
    return s.ready();
  }
  if (action == NativeCyclesCompletionAction::Consume) {
    // Retire immediately, before even commit-vector allocation can fail. The
    // CPU worker no longer accesses readback, but identity keepers stay owner-held.
    auto *batch = s.retire(s.worker.get());
    struct Release {
      CpuBatch *batch;
      Impl *owner;
      ~Release()
      {
        owner->capture_keepers.clear();
        {
          std::lock_guard lock(owner->retire_mutex);
          batch->owner_done = true;
        }
        owner->retire_changed.notify_all();
      }
    } release{batch, &s};
    s.readback.reset();
    s.targets.clear();
    s.captures.clear();
    struct Commit {
      NativeCyclesRevision *state;
      std::uint64_t captured;
      Image *image;
    };
    std::vector<Commit> commits;
    bool current = active && !paused;
    if (current) {
      commits.reserve(batch->captures.size());
      for (const auto &capture : batch->captures) {
        const auto &id = *capture.identity;
        auto found = s.entries.find(id.image);
        auto *image = find_image(main, id.image);
        if (!image || found == s.entries.end() || !found->second.identity) {
          current = false;
          continue;
        }
        auto &entry = found->second;
        current &= native_cycles_publishable(entry.revision,
                                             s.entry_epoch,
                                             capture.entry_epoch,
                                             capture.revision,
                                             *entry.identity,
                                             id,
                                             active);
        commits.push_back({&entry.revision, capture.revision, image});
      }
    }
    if (current) {
      const auto publish_started = Clock::now();
      // Prepare bounded diagnostic metadata before the allocation-free live commit.
      // These are already-owned publication buffers, never a cache load/acquire.
      decltype(s.bk_packets) bk_candidate;
      const bool bk_diagnostic = std::getenv("BTS_BK_FOLDER_MASK") != nullptr ||
                                 std::getenv("BTS_BJ_PROJECTION_UI") != nullptr ||
                                 std::getenv("BTS_BM_ADJUSTMENT_DEMO") != nullptr;
      if (bk_diagnostic) {
        bk_candidate = s.bk_packets;
        for (const auto &tile : batch->entries) {
          const auto capture = std::find_if(
              batch->captures.begin(), batch->captures.end(), [&](const auto &value) {
                return value.identity->image == tile.image;
              });
          if (capture == batch->captures.end())
            throw std::runtime_error("BK publication packet identity missing");
          const auto &identity = *capture->identity;
          if (identity.role != 0 && identity.role != 2)
            continue;
          const auto *buffer = tile.buffer;
          if (!buffer || !buffer->float_data() || buffer->channels != 4 || buffer->x < 1 ||
              buffer->y < 1)
            throw std::runtime_error("BK publication packet is not Float32 RGBA");
          QJsonArray samples;
          for (int part : {1, 3, 2}) {
            const int x = std::min(buffer->x - 1, buffer->x * part / 4), y = buffer->y / 2;
            QJsonArray rgba;
            for (int c = 0; c < 4; ++c) {
              const auto value = buffer->float_data()[(std::size_t(y) * buffer->x + x) * 4 + c];
              if (!std::isfinite(value))
                throw std::runtime_error("BK publication packet has nonfinite pixels");
              rgba.append(value);
            }
            samples.append(QJsonObject{{"xy", QJsonArray{x, y}}, {"rgba", rgba}});
          }
          bk_candidate[{tile.image, tile.tile}] = {
              {"image", QString::number(tile.image)},
              {"material", QString::number(identity.material)},
              {"role", identity.role},
              {"tile", tile.tile},
              {"packet_revision", QString::number(capture->revision)},
              {"packet_epoch", QString::number(capture->entry_epoch)},
              {"allocation", QString::number(identity.allocation, 16)},
              {"width", buffer->x},
              {"height", buffer->y},
              {"samples", samples}};
        }
      }
      publish_native_cycles_cache(
          batch->entries,
          batch->images,
          s.publication_attempted,
          [&](auto id, int tile, ImBuf *buffer, ImBuf **previous) {
            return BKE_image_publish_hosted_gpu_buffer(
                find_image(main, id), tile, buffer, previous);
          },
          [&](auto id) {
            for (const auto &commit : commits)
              if (commit.image->id.session_uid == id) {
                DEG_id_tag_update_ex(&main, &commit.image->id, ID_RECALC_SOURCE);
                break;
              }
          });
      for (const auto &commit : commits)
        native_cycles_commit_revision(*commit.state, commit.captured);
      s.publication_attempted = false;
      if (bk_diagnostic)
        s.bk_packets.swap(bk_candidate);
      if (tracing())
        std::printf(
            "BTS_CYCLES_MIRROR_PUBLISH images=%zu tiles=%zu cpu_bytes=%llu owner_ms=%.6f cpu_composite=0\n",
            batch->images.size(),
            batch->entries.size(),
            static_cast<unsigned long long>(batch->bytes),
            milliseconds(publish_started));
      // New wanted work starts on a later owner tick: no next-capture admission
      // or allocation follows this live cache/notification/revision commit.
      return s.ready();
    }
  }
  if (paused) {
    if (!s.worker.valid())
      s.clear_capture();
    return s.ready();
  }
  if (s.readback) {
    if (!active) {
      s.clear_capture();
      return true;
    }
    const auto poll_started = Clock::now();
    const bool complete = s.readback->poll();
    if (tracing())
      std::printf("BTS_CYCLES_MIRROR_POLL tiles=%zu ready=%d owner_ms=%.6f\n",
                  s.targets.size(),
                  int(complete),
                  milliseconds(poll_started));
    if (!complete)
      return s.ready();
    auto *capture = s.readback.get();
    auto targets = s.targets;
    auto images = s.captures;
    s.worker = std::async(
        std::launch::async, [capture, targets = std::move(targets), images = std::move(images)] {
          struct Release {
            GpuProjectReadback *capture;
            ~Release()
            {
              capture->release_cpu();
            }
          } release{capture};
          // Readback arrays already exist and are excluded from MemAvailable here.
          // This phase allocates only the immutable float4 ImBuf copies.
          std::uint64_t bytes = 0;
          for (const auto &t : targets)
            bytes = resource_sum(
                bytes, resource_product(resource_product(t.region.width, t.region.height), 16));
          auto result = std::make_unique<CpuBatch>();
          result->captures = images;
          result->buffers.reserve(targets.size());
          result->images.reserve(images.size());
          result->entries.reserve(targets.size());
          for (const auto &image : images)
            result->images.push_back(image.identity->image);
          for (std::size_t i = 0; i < targets.size(); ++i) {
            const auto &t = targets[i];
            const auto &id = *images.at(t.image_index).identity;
            const auto &image = capture->images().at(i);
            auto buffer = own(
                IMB_allocImBuf(t.region.width, t.region.height, ImBufFlags::FloatData));
            if (!buffer)
              throw std::bad_alloc();
            buffer->channels = 4;
            if (image.uniform) {
              for (std::size_t j = 0; j < std::size_t(t.region.width) * t.region.height; ++j)
                std::copy_n(image.pixels, 4, buffer->float_data_for_write() + j * 4);
            }
            else
              std::copy_n(image.pixels,
                          std::size_t(t.region.width) * t.region.height * 4,
                          buffer->float_data_for_write());
            IMB_colormanagement_assign_float_colorspace(buffer.get(), id.buffer_space.c_str());
            buffer->userflags |= IB_PERSISTENT;
            result->bytes = resource_sum(
                result->bytes,
                resource_product(resource_product(t.region.width, t.region.height), 16));
            result->entries.push_back({id.image, t.region.number, buffer.get(), nullptr});
            result->buffers.push_back(std::move(buffer));
          }
          return result;
        });
    return s.ready();
  }
  if (!active)
    return true;
  std::vector<Tile> targets;
  std::vector<CaptureImage> images;
  std::vector<OwnerRef> keepers;
  std::vector<const Identity *> allocations;
  std::uint64_t cpu = 0, gpu_bytes = 0, held_gpu_bytes = 0, total_tiles = 0;
  for (const auto &[uid, entry] : s.entries)
    if (entry.revision.wanted != entry.revision.published)
      total_tiles = resource_sum(total_tiles, entry.identity->tiles.size());
  const auto metadata = resource_sum(
      resource_product(s.entries.size(),
                       sizeof(CaptureImage) + sizeof(OwnerRef) + sizeof(const Identity *)),
      resource_product(total_tiles,
                       2 * sizeof(Tile) + sizeof(NativeCyclesCacheEntry) + sizeof(Buffer)));
  images.reserve(s.entries.size());
  keepers.reserve(s.entries.size());
  allocations.reserve(s.entries.size());
  targets.reserve(total_tiles);
  for (auto &[uid, entry] : s.entries) {
    if (entry.revision.wanted == entry.revision.published)
      continue;
    const auto &id = *entry.identity;
    const auto index = images.size();
    images.push_back({entry.identity, entry.revision.wanted, s.entry_epoch});
    keepers.push_back(keep(entry.keeper.get()));
    allocations.push_back(&id);
    const auto texel = id.format == int(gpu::TextureFormat::SFLOAT_16_16_16_16) ? 8 : 16;
    // New owner-phase CPU storage is readback float4 + later immutable ImBuf;
    // the old published cache is already excluded from available RAM.
    for (const auto &tile : id.tiles) {
      const auto size = resource_product(resource_product(tile.width, tile.height), 16);
      cpu = resource_sum(cpu, resource_product(size, 2));
      gpu_bytes = resource_sum(gpu_bytes,
                               resource_product(resource_product(tile.width, tile.height), texel));
      targets.push_back({index, tile});
    }
  }
  if (targets.empty())
    return s.ready();
  // Reference aliases do not duplicate telemetry bytes. Count each actual
  // complete held source allocation once, separately from new GPU copies.
  std::sort(allocations.begin(), allocations.end(), [](auto *a, auto *b) {
    return a->allocation < b->allocation;
  });
  std::uintptr_t previous = 0;
  for (const auto *id : allocations)
    if (id->allocation != previous) {
      previous = id->allocation;
      const auto texel = id->format == int(gpu::TextureFormat::SFLOAT_16_16_16_16) ? 8 : 16;
      int width = id->width, height = id->height;
      for (int mip = 0; mip < id->mips; ++mip) {
        held_gpu_bytes = resource_sum(
            held_gpu_bytes,
            resource_product(resource_product(resource_product(width, height), id->layers),
                             texel));
        width = std::max(1, width / 2);
        height = std::max(1, height / 2);
      }
    }
  cpu = resource_sum(cpu, metadata);
  // Driver free VRAM already excludes resident source allocations. Plain refs
  // keep them alive but allocate no new storage; charge only snapshot copies.
  s.ensure_retirement();
  const auto submit_started = Clock::now();
  auto capture = std::make_unique<GpuProjectReadback>();
  for (const auto &t : targets) {
    const auto &id = *images[t.image_index].identity;
    const auto &r = t.region;
    capture->submit(
        id.image, r.number, keepers[t.image_index].get(), r.x, r.y, r.layer, r.width, r.height);
  }
  if (tracing())
    std::printf(
        "BTS_CYCLES_MIRROR_CAPTURE tiles=%zu bytes=%llu held_source_bytes=%llu owner_ms=%.6f\n",
        targets.size(),
        static_cast<unsigned long long>(gpu_bytes),
        static_cast<unsigned long long>(held_gpu_bytes),
        milliseconds(submit_started));
  s.targets = std::move(targets);
  s.captures = std::move(images);
  s.capture_keepers = std::move(keepers);
  s.readback = std::move(capture);
  return s.ready();
}
catch (const std::exception &error) {
  auto &s = *impl_;
  s.failed_serial = s.serial;
  // After publication was attempted use the standing nonallocating warning.
  // Do not allocate new error-detail bookkeeping after a partial BKE mutation.
  if (s.publication_attempted)
    s.failure.clear();
  else
    try {
      s.failure = error.what();
    }
    catch (...) {
      s.failure.clear();
    }
  s.publication_attempted = false;
  if (!s.worker.valid())
    s.clear_capture();
  throw;
}
catch (...) {
  auto &s = *impl_;
  s.failed_serial = s.serial;
  s.failure.clear();
  s.publication_attempted = false;
  if (!s.worker.valid())
    s.clear_capture();
  throw;
}
}  // namespace bts
