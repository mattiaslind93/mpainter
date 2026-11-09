// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GeometryMask.hh"
#include "Performance.hh"
#include "TextureMemory.hh"
#include "GPU_texture.hh"
#include <memory>

namespace bts {
// Geometry ownership is immutable mesh metadata, shared by every layer/channel.
// Inclusion decisions and pixel coverage stay in the layer shader.
class GpuGeometryMask {
  using Texture = std::unique_ptr<blender::gpu::Texture, decltype(&blender::GPU_texture_free)>;
  struct Atlas {
    std::shared_ptr<const GeometryOwnershipTile> source;
    Texture image{nullptr, blender::GPU_texture_free};
    int row = 0;
    std::uint64_t used = 0;
  };
  struct Selection {
    Texture image{nullptr, blender::GPU_texture_free};
    std::vector<std::array<std::uint32_t, 2>> rows;
  };
  std::map<const GeometryOwnershipTile *, Atlas> atlases_;
  std::map<std::string, Selection> selections_;
  Texture empty_{nullptr, blender::GPU_texture_free};
  std::uint64_t used_ = 0, clock_ = 0;

 public:
  std::size_t atlas_count() const
  {
    return atlases_.size();
  }
  blender::gpu::Texture *empty()
  {
    using namespace blender;
    if (!empty_) {
      empty_.reset(GPU_texture_create_2d("BTS empty geometry ownership",
                                         1,
                                         1,
                                         1,
                                         gpu::TextureFormat::UINT_32,
                                         GPU_TEXTURE_USAGE_GENERAL,
                                         nullptr));
      if (!empty_)
        throw std::bad_alloc();
      const unsigned zero[4]{};
      GPU_texture_clear(empty_.get(), GPU_DATA_UINT, zero);
    }
    return empty_.get();
  }

  blender::gpu::Texture *atlas(const std::shared_ptr<const GeometryOwnershipTile> &source,
                               std::uint64_t &bytes,
                               performance::Clock::time_point deadline)
  {
    using namespace blender;
    performance::Scope timing("gpu_geometry_mask_upload");
    if (!source || source->width <= 0 || source->height <= 0 ||
        source->owners.size() != std::size_t(source->width) * source->height)
    {
      throw std::runtime_error("Geometry mask requires a current UV ownership atlas");
    }
    if (!atlases_.contains(source.get())) {
      const auto needed = source->owners.size() * sizeof(std::uint32_t);
      const auto free = texture_free_vram.load();
      const auto budget = free ? resource_sum(used_, free) : texture_available_ram_bytes();
      while (used_ + needed > budget) {
        auto victim = atlases_.end();
        for (auto it = atlases_.begin(); it != atlases_.end(); ++it)
          if (victim == atlases_.end() || it->second.used < victim->second.used)
            victim = it;
        if (victim == atlases_.end())
          throw std::bad_alloc();
        used_ -= victim->second.source->owners.size() * sizeof(std::uint32_t);
        atlases_.erase(victim);
      }
      Atlas entry;
      entry.source = source;
      entry.image.reset(GPU_texture_create_2d("BTS shared geometry ownership",
                                              source->width,
                                              source->height,
                                              1,
                                              gpu::TextureFormat::UINT_32,
                                              GPU_TEXTURE_USAGE_GENERAL,
                                              nullptr));
      if (!entry.image)
        throw std::bad_alloc();
      atlases_.emplace(source.get(), std::move(entry));
      used_ += needed;
    }
    auto &entry = atlases_.at(source.get());
    entry.used = ++clock_;
    while (entry.row < source->height) {
      if (performance::Clock::now() >= deadline)
        return nullptr;
      const int rows = std::min(
          {64, source->height - entry.row, int(bytes / (std::uint64_t(source->width) * 4))});
      if (!rows)
        return nullptr;
      GPU_texture_update_sub(entry.image.get(),
                             GPU_DATA_UINT,
                             source->owners.data() + std::size_t(entry.row) * source->width,
                             0,
                             entry.row,
                             0,
                             source->width,
                             rows,
                             1);
      bytes -= std::uint64_t(source->width) * rows * 4;
      entry.row += rows;
    }
    return entry.image.get();
  }

  blender::gpu::Texture *selection(const std::string &key,
                                   const std::map<std::uint32_t, std::string> &names,
                                   const GeometryMask &mask)
  {
    using namespace blender;
    std::vector<std::array<std::uint32_t, 2>> rows;
    for (const auto &[id, name] : names)
      rows.push_back({id, std::uint32_t(mask.includes(name))});
    if (rows.empty())
      rows.push_back({0, 0});
    auto &entry = selections_[key];
    const bool resize = !entry.image || GPU_texture_width(entry.image.get()) != int(rows.size());
    if (resize)
      entry.image.reset(GPU_texture_create_2d("BTS geometry inclusion metadata",
                                              int(rows.size()),
                                              1,
                                              1,
                                              gpu::TextureFormat::UINT_32_32,
                                              GPU_TEXTURE_USAGE_GENERAL,
                                              nullptr));
    if (!entry.image)
      throw std::bad_alloc();
    if (resize || entry.rows != rows) {
      GPU_texture_update(entry.image.get(), GPU_DATA_UINT, rows.data());
      entry.rows = std::move(rows);
    }
    return entry.image.get();
  }
};
}  // namespace bts
