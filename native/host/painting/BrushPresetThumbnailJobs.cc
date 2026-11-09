// SPDX-License-Identifier: GPL-3.0-or-later
#include <epoxy/gl.h>
#include "Theme.hh"
#include "BrushPresetThumbnailJobs.hh"
#include "BrushPresetThumbnailStroke.hh"
#include "BlenderHost.hh"
#include "GpuBrushRaster.hh"
#include "GpuThumbnail.hh"
#include "Performance.hh"
#include "PaintRecording.hh"
#include <optional>
#include <cstdio>
#include <QTimer>
#include <QCryptographicHash>
#include <deque>
#include <map>
#include <cmath>
namespace bts {
namespace {
constexpr int side = 128, batch_size = 32;
QImage brush_thumbnail_image(const ThumbnailMetadata &packet)
{
  QImage image(ThumbnailMetadata::side, ThumbnailMetadata::side, QImage::Format_RGB32);
  for (int y = 0; y < image.height(); ++y)
    for (int x = 0; x < image.width(); ++x) {
      const auto &p = packet.pixels[y * image.width() + x];
      const auto byte = [](float v) {
        if (!std::isfinite(v))
          throw std::runtime_error("Nonfinite brush thumbnail packet");
        return qRound(255 * std::pow(std::clamp(v, 0.f, 1.f), 1 / 2.2f));
      };
      image.setPixel(x, y, qRgb(byte(p[0]), byte(p[1]), byte(p[2])));
    }
  return image;
}
}  // namespace
struct BrushPresetThumbnailJobs::Impl : std::enable_shared_from_this<Impl> {
  using Texture = std::unique_ptr<blender::gpu::Texture, decltype(&blender::GPU_texture_free)>;
  struct Gpu {
    GpuBrushRaster raster;
    GpuThumbnail download;
    bool prepared = false;
    Texture ink{nullptr, blender::GPU_texture_free},
        background{nullptr, blender::GPU_texture_free},
        display{nullptr, blender::GPU_texture_free};
    void initialize()
    {
      using namespace blender;
      if (prepared)
        return;
      for (auto *image : {&ink, &background, &display})
        image->reset(GPU_texture_create_2d("Own brush stroke preview",
                                           side,
                                           side,
                                           1,
                                           gpu::TextureFormat::SFLOAT_32_32_32_32,
                                           GPU_TEXTURE_USAGE_GENERAL,
                                           nullptr));
      if (!ink || !background || !display)
        throw std::bad_alloc();
      if (!raster.prepare())
        throw std::runtime_error("Brush thumbnail GPU raster preparation failed");
      // The download packet encodes with gamma 2.2. Decode the UI token with
      // the inverse so the preview canvas matches the surrounding panel exactly.
      const QColor canvas(theme::Surface);
      const float clear[4] = {}, surface[4] = {std::pow(float(canvas.redF()), 2.2f),
                                               std::pow(float(canvas.greenF()), 2.2f),
                                               std::pow(float(canvas.blueF()), 2.2f),
                                               1};
      GPU_texture_clear(ink.get(), GPU_DATA_FLOAT, clear);
      GPU_texture_clear(background.get(), GPU_DATA_FLOAT, surface);
      GPU_texture_clear(display.get(), GPU_DATA_FLOAT, surface);
      // Compile/allocate the small packet path during real host startup too.
      download.update({}, {{display.get(), side, side}}, 0);
      prepared = true;
    }
  };
  struct Version {
    std::uint64_t generation = 0, revision = 0;
    BrushThumbnailSourceKey key;
  };
  struct Request {
    StudioAsset asset;
    BrushThumbnailSourceKey key;
    std::uint64_t generation = 0;
    std::vector<std::function<void(QImage)>> complete;
    performance::Clock::time_point queued = performance::Clock::now();
  };
  BlenderHost &host;
  std::function<bool()> ready;
  std::function<void(QString)> error;
  QTimer timer;
  std::unique_ptr<Gpu> gpu;
  std::deque<Request> queue;
  std::unique_ptr<Request> active;
  std::optional<BrushThumbnailStroke> stroke;
  bool diagnostic = qEnvironmentVariableIsSet("BTS_BO_PRESET_DEMO");
  QJsonObject completed;
  std::uint64_t stamp_batches = 0, opacity_dispatches = 0, packet_submissions = 0,
                fence_packets = 0, cpu_materializations = 0;
  std::map<QUuid, Version> latest;
  std::uint64_t generation = 0, next_ticket = 0, ticket = 0;
  std::size_t next_stamp = 0;
  bool downloading = false, stopped = false;
  Impl(BlenderHost &h, std::function<bool()> r, std::function<void(QString)> e)
      : host(h), ready(std::move(r)), error(std::move(e))
  {
    timer.setInterval(16);
    QObject::connect(&timer, &QTimer::timeout, &timer, [this] { poll(); });
  }
  bool current(const Request &request) const
  {
    const auto found = latest.find(request.asset.id);
    return found != latest.end() && found->second.generation == request.generation;
  }
  void prepare_startup()
  {
    if (stopped)
      return;
    performance::Scope scope("brush_thumbnail_startup");
    host.with_gpu([&] {
      if (!gpu)
        gpu = std::make_unique<Gpu>();
      gpu->initialize();
    });
  }
  void clear_active()
  {
    active.reset();
    stroke.reset();
    next_stamp = 0;
    downloading = false;
    ticket = 0;
  }
  void poll()
  {
    const auto hold = shared_from_this();
    if (stopped || !ready() || stopped)
      return;
    const auto cpu_before = local_paint_materializations;
    performance::Scope scope("brush_thumbnail_tick");
    std::optional<QImage> result;
    QString failure_message;
    try {
      if (!active) {
        if (queue.empty()) {
          timer.stop();
          return;
        }
        active = std::make_unique<Request>(std::move(queue.front()));
        queue.pop_front();
        if (!current(*active))
          result = QImage{};
        else
          stroke = brush_thumbnail_stroke(*active->asset.brush_preset);
      }
      if (!result)
        host.with_gpu([&] {
          if (!gpu)
            gpu = std::make_unique<Gpu>();
          gpu->initialize();
          // get() retains the previous packet: require the current fence and ticket.
          if (gpu->download.pending()) {
            gpu->download.poll();
            if (gpu->download.pending())
              return;
          }
          if (!current(*active)) {
            result = QImage{};
            return;
          }
          if (downloading) {
            const auto packet = gpu->download.get();
            if (packet && packet->ticket == ticket) {
              if (diagnostic)
                ++fence_packets;
              result = brush_thumbnail_image(*packet);
            }
            return;
          }
          using namespace blender;
          if (next_stamp == 0) {
            const float clear[4] = {};
            GPU_texture_clear(gpu->ink.get(), GPU_DATA_FLOAT, clear);
            GPU_texture_copy(gpu->display.get(), gpu->background.get());
          }
          if (next_stamp < stroke->stamps.size()) {
            std::array<const PaintPixels::Stamp *, batch_size> commands{};
            const auto count = std::min<std::size_t>(batch_size,
                                                     stroke->stamps.size() - next_stamp);
            for (std::size_t i = 0; i < count; ++i)
              commands[i] = &stroke->stamps[next_stamp + i];
            if (!gpu->raster.stamps(gpu->ink.get(), 0, std::span(commands.data(), count), side))
              throw std::runtime_error("Brush thumbnail has an unsupported native stamp");
            next_stamp += count;
            if (diagnostic)
              ++stamp_batches;
            // At most one production stamp batch per owner tick.
            if (next_stamp < stroke->stamps.size())
              return;
          }
          if (!gpu->raster.over(gpu->display.get(),
                                gpu->background.get(),
                                gpu->ink.get(),
                                stroke->opacity,
                                {0, 0, side, side},
                                false,
                                false))
            throw std::runtime_error("Brush thumbnail stroke opacity failed");
          if (diagnostic && stroke->opacity > 0)
            ++opacity_dispatches;
          ticket = ++next_ticket;
          gpu->download.update({}, {{gpu->display.get(), side, side}}, ticket);
          downloading = true;
          if (diagnostic)
            ++packet_submissions;
        });
    }
    catch (const std::exception &failure) {
      failure_message = QString::fromUtf8(failure.what());
      if (gpu)
        host.with_gpu([&] { gpu.reset(); });
      result = QImage{};
    }
    if (!failure_message.isEmpty() && error)
      error(failure_message);
    if (stopped)
      return;
    if (result) {
      if (performance::enabled())
        std::printf(
            "BTS_BRUSH_THUMBNAIL_READY asset=%s revision=%llu current=%d request_to_ready_ms=%.3f stamps=%zu valid=%d\n",
            qPrintable(active->asset.name),
            (unsigned long long)active->asset.revision,
            int(current(*active)),
            performance::ms(active->queued),
            stroke ? stroke->stamps.size() : 0,
            !result->isNull());
      if (diagnostic && current(*active) && !result->isNull()) {
        completed[active->asset.id.toString(QUuid::WithoutBraces)] = QJsonObject{
            {"revision", QString::number(active->asset.revision)},
            {"image_sha256",
             QString::fromLatin1(
                 QCryptographicHash::hash(
                     QByteArrayView(reinterpret_cast<const char *>(result->constBits()),
                                    result->sizeInBytes()),
                     QCryptographicHash::Sha256)
                     .toHex())},
            {"width", result->width()},
            {"height", result->height()},
            {"format", int(result->format())},
            {"ticket", QString::number(ticket)}};
      }
      const auto id = active->asset.id;
      const auto completed_generation = active->generation;
      auto callbacks = std::move(active->complete);
      clear_active();
      for (auto &callback : callbacks) {
        if (stopped)
          break;
        const auto found = latest.find(id);
        const bool publish = found != latest.end() &&
                             found->second.generation == completed_generation;
        try {
          callback(publish ? *result : QImage{});
        }
        catch (const std::exception &failure) {
          if (error)
            error(QString::fromUtf8(failure.what()));
        }
      }
    }
    if (diagnostic)
      cpu_materializations += local_paint_materializations - cpu_before;
    if (!active && queue.empty())
      timer.stop();
  }
  void request(const StudioAsset &asset, std::function<void(QImage)> complete)
  {
    if (stopped) {
      complete({});
      return;
    }
    if (!asset.brush_preset)
      throw std::invalid_argument("Brush thumbnail requires a hydrated preset snapshot");
    auto key = brush_thumbnail_source_key(*asset.brush_preset);
    auto found = latest.find(asset.id);
    if (found != latest.end() && asset.revision < found->second.revision) {
      complete({});
      return;
    }
    if (found == latest.end() || found->second.revision != asset.revision ||
        found->second.key != key)
    {
      found = latest.insert_or_assign(asset.id, Version{++generation, asset.revision, key}).first;
      if (diagnostic)
        completed.remove(asset.id.toString(QUuid::WithoutBraces));
    }
    const auto wanted = found->second.generation;
    if (active && active->asset.id == asset.id && active->generation == wanted) {
      active->complete.push_back(std::move(complete));
      return;
    }
    for (auto &request : queue)
      if (request.asset.id == asset.id && request.generation == wanted) {
        request.complete.push_back(std::move(complete));
        return;
      }
    queue.push_back({asset, std::move(key), wanted, {std::move(complete)}});
    timer.start();
  }
  void shutdown()
  {
    if (stopped)
      return;
    stopped = true;
    timer.stop();
    queue.clear();
    clear_active();
    latest.clear();
    if (gpu)
      host.with_gpu([&] { gpu.reset(); });
  }
};
BrushPresetThumbnailJobs::BrushPresetThumbnailJobs(BlenderHost &host,
                                                   std::function<bool()> ready,
                                                   std::function<void(QString)> error)
    : impl_(std::make_shared<Impl>(host, std::move(ready), std::move(error)))
{
}
BrushPresetThumbnailJobs::~BrushPresetThumbnailJobs()
{
  shutdown();
}
void BrushPresetThumbnailJobs::prepare_startup()
{
  impl_->prepare_startup();
}
void BrushPresetThumbnailJobs::request(const StudioAsset &asset,
                                       std::function<void(QImage)> complete)
{
  impl_->request(asset, std::move(complete));
}
bool BrushPresetThumbnailJobs::pending() const
{
  return impl_->active || !impl_->queue.empty();
}
QJsonObject BrushPresetThumbnailJobs::diagnostics() const
{
  const auto &s = *impl_;
  if (!s.diagnostic)
    return {};
  QJsonArray completed;
  for (auto it = s.completed.begin(); it != s.completed.end(); ++it) {
    auto row = it.value().toObject();
    row["id"] = it.key();
    completed.append(row);
  }
  return {{"completed", completed},
          {"pending", pending()},
          {"dispatches", qint64(s.stamp_batches + s.opacity_dispatches + s.packet_submissions)},
          {"packets", qint64(s.fence_packets)},
          {"stamp_batches", qint64(s.stamp_batches)},
          {"opacity_dispatches", qint64(s.opacity_dispatches)},
          {"packet_submissions", qint64(s.packet_submissions)},
          {"matching_fence_packets", qint64(s.fence_packets)},
          {"cpu_materializations", qint64(s.cpu_materializations)},
          {"renderer",
           "Production GpuBrushRaster + GpuThumbnail fenced SSBO; neutral footprint reference"}};
}
void BrushPresetThumbnailJobs::shutdown()
{
  impl_->shutdown();
}
}  // namespace bts
