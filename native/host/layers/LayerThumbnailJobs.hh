// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "LayerThumbnail.hh"
#include "EvaluationScope.hh"
#include "Performance.hh"
#include <QJsonDocument>
#include <QThreadPool>
#include <QTimer>
#include <atomic>
#include <optional>
namespace bts {
// Latest-request-wins GPU thumbnails over immutable metadata snapshots.
// CPU work is limited to display conversion of the small returned packet.
class LayerThumbnailJobs {
  struct Task {
    QUuid layer;
    bool mask = false;
    QString slot;
    QByteArray revision;
    QImage image;
  };
  struct Request {
    std::shared_ptr<const TextureSet> set;
    int channel = 0;
    QByteArray signature;
    std::vector<Task> tasks;
  };
  struct Job {
    Request request;
    std::size_t index = 0;
    std::uint64_t ticket = 0;
  };
  struct Entry {
    QByteArray wanted, cached;
    QImage image;
  };
  std::map<QString, Entry> cache_;
  std::shared_ptr<Job> active_;
  QTimer poll_;
  static QThreadPool &pool()
  {
    static QThreadPool pool;
    return pool;
  }
  static QString slot(const QUuid &set, const QUuid &layer, int channel, bool mask)
  {
    return set.toString() + layer.toString() + QString::number(channel) +
           (mask ? "mask" : "content");
  }
  void retire()
  {
    if (active_)
      pool().start([retired = std::move(active_)] {});
  }
  void submit()
  {
    if (!active_ || active_->index >= active_->request.tasks.size()) {
      retire();
      poll_.stop();
      return;
    }
    const auto &task = active_->request.tasks[active_->index];
    if (!submit_request || !read_result) {
      retire();
      poll_.stop();
      return;
    }
    active_->ticket = submit_request(
        *active_->request.set, ThumbnailRequest{task.layer, active_->request.channel, task.mask});
  }
  void start(Request request)
  {
    retire();
    active_ = std::make_shared<Job>();
    active_->request = std::move(request);
    submit();
    if (active_)
      poll_.start();
  }
  void poll()
  {
    if (!active_)
      return;
    const auto packet = read_result();
    if (!packet || packet->ticket != active_->ticket)
      return;
    auto &job = *active_;
    auto &task = job.request.tasks[job.index];
    auto found = cache_.find(task.slot);
    if (packet->valid && found != cache_.end() && found->second.wanted == task.revision) {
      performance::Scope timing("thumbnail_display_conversion");
      auto image = thumbnail_image(*packet,
                                   ThumbnailRequest{task.layer, job.request.channel, task.mask});
      found->second.cached = task.revision;
      found->second.image = image;
      if (updated)
        updated(job.request.set->id, job.request.channel, task.layer, task.mask, image);
      std::printf(
          "BTS_GPU_THUMBNAIL_READY ticket=%llu channel=%d mask=%d packet_bytes=36864 cpu_composition=0\n",
          (unsigned long long)packet->ticket,
          job.request.channel,
          int(task.mask));
    }
    else if (!packet->valid)
      std::fprintf(stderr,
                   "BTS_THUMBNAIL_ERROR layer=%s GPU source unavailable\n",
                   task.layer.toString().toStdString().c_str());
    ++job.index;
    submit();
  }

 public:
  std::function<void(const QUuid &, int, const QUuid &, bool, const QImage &)> updated;
  std::function<std::uint64_t(const TextureSet &, const ThumbnailRequest &)> submit_request;
  std::function<std::optional<ThumbnailMetadata>()> read_result;
  LayerThumbnailJobs()
  {
    pool().setMaxThreadCount(1);
    poll_.setInterval(16);
    QObject::connect(&poll_, &QTimer::timeout, &poll_, [this] { poll(); });
  }
  ~LayerThumbnailJobs()
  {
    poll_.stop();
    retire();
    pool().waitForDone();
  }
  void request(const TextureSet &set, int channel)
  {
    if (!set.fully_loaded()) {
      retire();
      poll_.stop();
      cache_.clear();
      return;
    }
    performance::Scope timing("thumbnail_revision_metadata");
    EditStateScope metadata;
    auto state = QJsonDocument::fromJson(TextureDocument::mask_dependency_signature(set)).object();
    for (const char *key : {"selected_layer",
                            "selected_mask",
                            "selected_geometry",
                            "selected_effect",
                            "next_fill_number",
                            "next_paint_number",
                            "next_folder_number"})
      state.remove(key);
    auto common = state;
    common.remove("layers");
    const auto common_key = QJsonDocument(common).toJson(QJsonDocument::Compact);
    const auto dependency_key = QJsonDocument(state).toJson(QJsonDocument::Compact);
    std::map<QUuid, QJsonObject> layers;
    for (const auto &value : state["layers"].toArray()) {
      auto layer = value.toObject();
      layers[QUuid(layer["id"].toString())] = std::move(layer);
    }
    Request request;
    request.channel = channel;
    std::set<QString> live;
    for (const auto &layer : set.layers)
      for (bool mask : {false, true}) {
        if (mask && !layer.mask)
          continue;
        const auto key = slot(set.id, layer.id, channel, mask);
        live.insert(key);
        auto value = layers[layer.id];
        if (!mask)
          value.remove("mask");
        QByteArray revision = common_key + QByteArray::number(channel) +
                              QJsonDocument(value).toJson(QJsonDocument::Compact);
        if (mask && layer.mask->channel_dependent())
          revision += dependency_key;
        auto &entry = cache_[key];
        entry.wanted = revision;
        if (entry.cached != revision) {
          request.signature += key.toUtf8() + revision;
          request.tasks.push_back({layer.id, mask, key, std::move(revision), {}});
        }
      }
    std::erase_if(cache_, [&](const auto &entry) { return !live.contains(entry.first); });
    if (request.tasks.empty()) {
      retire();
      poll_.stop();
      return;
    }
    if (active_ && active_->request.signature == request.signature)
      return;
    request.set = std::make_shared<TextureSet>(set);
    start(std::move(request));
  }
  bool pending() const noexcept
  {
    return bool(active_);
  }
  QImage image(const QUuid &set, const QUuid &layer, int channel, bool mask) const
  {
    const auto found = cache_.find(slot(set, layer, channel, mask));
    return found == cache_.end() ? QImage{} : found->second.image;
  }
};
}  // namespace bts
