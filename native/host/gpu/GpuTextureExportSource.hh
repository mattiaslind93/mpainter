// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "BlenderHost.hh"
#include "LayerCapture.hh"
#include "TextureExportDialog.hh"
#include "NativeExportGuard.hh"
#include "ResourceAvailability.hh"
#include <QObject>
#include <QTimer>
#include <QElapsedTimer>
#include <QThreadPool>
#include <cstring>
namespace bts {
// Explicit Export owner. Only bounded fence-signalled packets cross to RAM;
// the worker receives completed immutable planes and never evaluates the stack.
class GpuTextureExportSource final : public QObject {
  BlenderHost &host_;
  std::function<void()> redraw_;
  std::shared_ptr<const std::atomic_bool> cancelled_;
  std::function<void(std::vector<ExportTile>)> complete_;
  std::function<void(std::string)> failed_;
  bool material_ids_ = false;
  std::map<QUuid, std::shared_ptr<std::vector<float>>> id_coverage_;
  std::vector<ExportTile> tiles_;
  QTimer poll_;
  QElapsedTimer progress_;
  std::shared_ptr<ExportCapturedPlanes> planes_;
  std::shared_ptr<std::vector<float>> pixels_;
  struct Allocation {
    std::shared_ptr<std::vector<float>> pixels;
    std::string error;
    std::atomic_bool ready = false;
    double milliseconds = 0;
  };
  std::shared_ptr<Allocation> allocation_;
  double max_assembly_ms_ = 0, allocation_ms_ = 0;
  std::size_t tile_ = 0, channel_ = 0, packets_ = 0;
  std::vector<int> channels_;
  int x_ = 0, y_ = 0;
  std::uint64_t ticket_ = 0;
  LayerCaptureRequest chunk_;
  bool finished_ = false;
  static bool needs_gpu_capture(const ExportTile &tile)
  {
    return tile.gpu_capture_planned || texture_export_has_adjustment(tile.set) ||
           texture_export_needs_native_capture(tile.set, tile.number);
  }
  void finish(std::string error = {})
  {
    if (finished_)
      return;
    finished_ = true;
    poll_.stop();
    auto complete = std::move(complete_);
    auto failed = std::move(failed_);
    deleteLater();
    if (error.empty()) {
      if (qEnvironmentVariableIsSet("BTS_BM_ADJUSTMENT_DEMO") && parent())
        parent()->setProperty("bmExportGpuStats",
                              QJsonObject{{"tiles", qint64(tiles_.size())},
                                          {"packets", qint64(packets_)},
                                          {"worker_allocation_ms", allocation_ms_},
                                          {"max_owner_assembly_ms", max_assembly_ms_}});
      std::printf(
          "BTS_ADJUSTMENT_EXPORT_GPU_CAPTURE tiles=%zu packets=%zu worker_allocation_ms=%.3f max_owner_assembly_ms=%.3f cpu_composite=0\n",
          tiles_.size(),
          packets_,
          allocation_ms_,
          max_assembly_ms_);
      complete(std::move(tiles_));
    }
    else
      failed(std::move(error));
  }
  void next_plane()
  {
    auto &tile = tiles_.at(tile_);
    if (!planes_) {
      planes_ = std::make_shared<ExportCapturedPlanes>();
      planes_->width = tile.resolution;
      planes_->height = tile.rows();
      channels_ = {0, 1, 2, 3, 4, 5, -1, -2};
      if (tile.set.displacement.channel)
        channels_.push_back(16);
      channel_ = 0;
      id_coverage_.clear();
      if (material_ids_) {
        const auto &layers = evaluation_layers(tile.set);
        for (int i = 0; i < int(layers.size()); ++i) {
          const auto &layer = layers[i];
          if (!layer.visible)
            continue;
          for (int c = 0; c < 6; ++c)
            if (c == 5 ? (tile.set.ao_channel && layer.ao_enabled && layer.ao_opacity > 0 &&
                          layer.ao_blending != LayerBlend::Disabled) :
                         (tile.set.channels[c] && layer.channels[c] && layer.opacity[c] > 0 &&
                          layer.blending[c] != LayerBlend::Disabled))
              channels_.push_back(100 + i * 7 + c);
          if (layer.mask && layer.mask->enabled)
            channels_.push_back(100 + i * 7 + 6);
        }
      }
    }
    if (channels_[channel_] >= 100) {
      const auto &layer = evaluation_layers(tile.set).at((channels_[channel_] - 100) / 7);
      if (id_coverage_.contains(layer.id)) {
        pixels_ = id_coverage_.at(layer.id);
        x_ = y_ = 0;
        return;
      }
    }
    const auto count = resource_product(
        resource_product(std::uint64_t(tile.resolution), tile.rows()), 4);
    allocation_ = std::make_shared<Allocation>();
    auto allocation = allocation_;
    auto cancel = cancelled_;
    QThreadPool::globalInstance()->start([allocation, cancel, count] {
      QElapsedTimer clock;
      clock.start();
      try {
        if (!cancel->load())
          allocation->pixels = std::make_shared<std::vector<float>>(count);
      }
      catch (const std::exception &error) {
        allocation->error = error.what();
      }
      allocation->milliseconds = clock.nsecsElapsed() / 1e6;
      allocation->ready.store(true, std::memory_order_release);
    });
    x_ = y_ = 0;
  }
  void tick()
  {
    try {
      if (finished_)
        return;
      // The inspection owner retains its copied request and drains any submitted
      // fence independently. Detach immediately; never wait on that fence here.
      if (cancelled_->load()) {
        finish("Export cancelled during GPU capture");
        return;
      }
      if (ticket_) {
        const auto packet = host_.layer_capture_result();
        if (!packet || packet->ticket != ticket_) {
          if (progress_.elapsed() > 60000)
            throw std::runtime_error("GPU Export capture made no progress for 60 seconds");
          redraw_();
          return;
        }
        if (!packet->valid || !packet->pixels ||
            packet->pixels->size() != std::size_t(chunk_.width) * chunk_.height)
          throw std::runtime_error("GPU Export returned an invalid bounded packet");
        ticket_ = 0;
        ++packets_;
        progress_.restart();
        if (cancelled_->load()) {
          finish("Export cancelled during GPU capture");
          return;
        }
        QElapsedTimer assembly;
        assembly.start();
        const auto width = tiles_[tile_].resolution;
        if (channels_[channel_] >= 100) {
          const int code = channels_[channel_] - 100, c = code % 7;
          const auto &layer = evaluation_layers(tiles_[tile_].set).at(code / 7);
          const float opacity = c == 6 ? 1 : c == 5 ? layer.ao_opacity : layer.opacity[c];
          for (int row = 0; row < chunk_.height; ++row)
            for (int col = 0; col < chunk_.width; ++col) {
              const auto &sample = packet->pixels->at(std::size_t(row) * chunk_.width + col);
              auto &coverage =
                  (*pixels_)[(std::size_t(chunk_.y + row) * width + chunk_.x + col) * 4];
              if (c == 6)
                coverage *= sample[0];
              else
                coverage = std::max(coverage, sample[3] * opacity);
            }
        }
        else
          for (int row = 0; row < chunk_.height; ++row)
            std::memcpy(pixels_->data() + (std::size_t(chunk_.y + row) * width + chunk_.x) * 4,
                        packet->pixels->data() + std::size_t(row) * chunk_.width,
                        std::size_t(chunk_.width) * sizeof(std::array<float, 4>));
        max_assembly_ms_ = std::max(max_assembly_ms_, assembly.nsecsElapsed() / 1e6);
        x_ += chunk_.width;
        if (x_ == width) {
          x_ = 0;
          y_ += chunk_.height;
        }
        if (y_ == tiles_[tile_].rows()) {
          if (channels_[channel_] >= 100) {
          }
          else if (channels_[channel_] == -2)
            planes_->normal_perturbation = pixels_;
          else if (channels_[channel_] < 0)
            planes_->mixed_ao = pixels_;
          else
            planes_->channels.at(channels_[channel_]) = pixels_;
          pixels_.reset();
          if (++channel_ == channels_.size()) {
            for (const auto &[id, coverage] : id_coverage_)
              planes_->material_id_coverage[id] = coverage;
            tiles_[tile_].captured = planes_;
            planes_.reset();
            ++tile_;
            while (tile_ < tiles_.size() && !needs_gpu_capture(tiles_[tile_]))
              ++tile_;
            if (tile_ == tiles_.size()) {
              finish();
              return;
            }
          }
          next_plane();
        }
      }
      if (cancelled_->load()) {
        finish("Export cancelled during GPU capture");
        return;
      }
      if (!pixels_) {
        if (!allocation_->ready.load(std::memory_order_acquire))
          return;
        if (!allocation_->error.empty())
          throw std::runtime_error(allocation_->error);
        pixels_ = std::move(allocation_->pixels);
        allocation_ms_ += allocation_->milliseconds;
        allocation_.reset();
        if (!pixels_)
          throw std::runtime_error("GPU Export plane allocation cancelled");
        progress_.restart();
        if (channels_[channel_] >= 100)
          id_coverage_[evaluation_layers(tiles_[tile_].set)
                           .at((channels_[channel_] - 100) / 7)
                           .id] = pixels_;
      }
      auto &tile = tiles_[tile_];
      chunk_ = {};
      chunk_.channel = channels_[channel_] < 0 ? 5 : channels_[channel_];
      chunk_.normal_perturbation = channels_[channel_] == -2;
      chunk_.authored_ao = channels_[channel_] == 5;
      chunk_.final_composite = true;
      chunk_.tile = tile.number;
      chunk_.x = x_;
      chunk_.y = y_;
      chunk_.width = std::min(256, tile.resolution - x_);
      chunk_.height = std::min(256, tile.rows() - y_);
      chunk_.canvas_width = tile.resolution;
      chunk_.canvas_height = tile.rows();
      if (channels_[channel_] >= 100) {
        const int code = channels_[channel_] - 100;
        chunk_.final_composite = false;
        chunk_.authored_ao = false;
        chunk_.normal_perturbation = false;
        chunk_.layer = evaluation_layers(tile.set).at(code / 7).id;
        chunk_.mask = code % 7 == 6;
        chunk_.channel = chunk_.mask ? 0 : code % 7;
      }
      ticket_ = host_.request_layer_capture(tile.set, chunk_);
      redraw_();
    }
    catch (const std::exception &error) {
      finish(error.what());
    }
  }
  // Reuse the ordinary metadata-only export source on frozen request sets.
  // This attaches immutable geometry/padding and authored Bump distance to ALL
  // selected tiles; non-BM tiles keep their established export-worker route.
  static std::vector<ExportTile> source_metadata(BlenderHost &host,
                                                 const TextureDocument &document,
                                                 const ExportRequest &request)
  {
    TextureDocument frozen = document;
    std::vector<TextureSet> sets;
    std::vector<QUuid> ids;
    for (const auto &tile : request.tiles)
      if (std::find(ids.begin(), ids.end(), tile.set.id) == ids.end()) {
        ids.push_back(tile.set.id);
        sets.push_back(tile.set);
      }
    frozen.stage_texture_sets(std::move(sets));
    const auto metadata = host.export_source(frozen, ids);
    if (metadata.size() != request.tiles.size())
      throw std::invalid_argument("GPU Export source tile membership changed");
    auto result = request.tiles;
    std::set<std::pair<QUuid, int>> seen;
    for (auto &tile : result) {
      const auto found = std::find_if(metadata.begin(), metadata.end(), [&](const auto &source) {
        return source.set.id == tile.set.id && source.number == tile.number;
      });
      if (found == metadata.end() || !seen.emplace(tile.set.id, tile.number).second ||
          found->resolution != tile.resolution || found->rows() != tile.rows())
        throw std::invalid_argument("GPU Export source identity or dimensions changed");
      tile.set = found->set;
      tile.normal = found->normal;
      tile.ao = found->ao;
      tile.coverage = found->coverage;
      tile.deferred_padding = found->deferred_padding;
      tile.bump_distance = found->bump_distance;
      if (!tile.deferred_padding)
        throw std::invalid_argument("GPU Export source has no immutable geometry padding");
    }
    return result;
  }

 public:
  GpuTextureExportSource(QObject *parent,
                         BlenderHost &host,
                         std::function<void()> redraw,
                         const TextureDocument &document,
                         const ExportRequest &request,
                         std::shared_ptr<const std::atomic_bool> cancel,
                         std::function<void(std::vector<ExportTile>)> complete,
                         std::function<void(std::string)> failed)
      : QObject(parent),
        host_(host),
        redraw_(std::move(redraw)),
        cancelled_(std::move(cancel)),
        complete_(std::move(complete)),
        failed_(std::move(failed)),
        tiles_(source_metadata(host, document, request))
  {
    material_ids_ = std::any_of(
        request.outputs.begin(), request.outputs.end(), [](const auto &output) {
          return output.source == ExportMapSource::MaterialId;
        });
    std::uint64_t bytes = 0;
    for (auto &tile : tiles_)
      if (needs_gpu_capture(tile)) {
        tile.set = host_.prepare_layer_capture(tile.set);
        bytes = resource_sum(
            bytes,
            resource_product(resource_product(std::uint64_t(tile.resolution), tile.rows()),
                             16ULL * (8 + int(tile.set.displacement.channel) +
                                      (material_ids_ ? evaluation_layers(tile.set).size() : 0))));
      }

    // Non-adjustment tiles retain their existing worker route. Capture every BM
    // tile, including hidden retained recipes after a PT toggle is switched off.
    while (tile_ < tiles_.size() && !needs_gpu_capture(tiles_[tile_]))
      ++tile_;
    QObject::connect(&poll_, &QTimer::timeout, this, [this] {
      if (tile_ < tiles_.size() && !needs_gpu_capture(tiles_[tile_])) {
        finish();
        return;
      }
      tick();
    });
    if (tile_ == tiles_.size()) {
      QTimer::singleShot(0, this, [this] { finish(); });
      return;
    }
    next_plane();
    progress_.start();
    poll_.start(16);
  }
};
}  // namespace bts
