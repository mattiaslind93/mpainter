#pragma once
#include "FillTextureIO.hh"
#include <QObject>
#include <QTimer>
#include <condition_variable>
#include <deque>
#include <functional>
#include <future>
#include <mutex>
namespace bts {
struct TextureLoadTarget {
  QUuid document, set, layer, insertion, effect;
  int channel = 0;
  std::set<int> tiles;
  bool create_fill = false;
  // A deferred mask destination is created only after decode and GPU preparation.
  bool create_mask_fill = false, selection_mask = false, selection_geometry = false;
  QUuid selection_effect;
  // A buffered material editor is a typed destination, never a fabricated layer.
  QUuid buffer;
  int material_input = -1;
  std::uint64_t revision = 0;
  bool same_slot(const TextureLoadTarget &other) const
  {
    return buffer == other.buffer && material_input == other.material_input &&
           document == other.document && set == other.set && layer == other.layer &&
           channel == other.channel && insertion == other.insertion &&
           (effect == other.effect ||
            (create_mask_fill && other.create_mask_fill && revision == other.revision &&
             selection_mask == other.selection_mask &&
             selection_geometry == other.selection_geometry &&
             selection_effect == other.selection_effect));
  }
};
struct TextureLoadStatus {
  QString text;
  int completed = 0, total = 0;
  bool loading = false, error = false;
};
struct LoadedTexture {
  TextureLoadTarget target;
  StudioAsset asset;
  std::shared_ptr<const FillTexture> texture;
  double worker_ms = 0;
};
struct TextureLoadHooks {
  std::function<bool(const TextureLoadTarget &)> valid;
  std::function<bool()> busy;
  std::function<void(const TextureLoadTarget &, const TextureLoadStatus &)> status;
  // Optional owner-frame GPU preparation. False keeps the document/Undo untouched.
  std::function<bool(const LoadedTexture &)> prepare;
  std::function<void()> release_preparation;
  std::function<void(LoadedTexture)> install;
  std::function<void(const QString &)> error;
};
class TextureLoadController final : public QObject {
 public:
  TextureLoadController(QObject *, TextureLoadHooks);
  ~TextureLoadController();
  void from_file(TextureLoadTarget, const QString &, const std::string &source_space = {});
  void from_asset(TextureLoadTarget, StudioAsset, const std::string &source_space = {});
  void cancel(const TextureLoadTarget &);
  void cancel_all();
  bool pending() const
  {
    return job_.valid() || !queue_.empty();
  }

 private:
  struct Request {
    TextureLoadTarget target;
    QString path;
    std::optional<StudioAsset> asset;
    std::string source_space;
  };
  struct Progress {
    std::mutex mutex;
    TextureDecodeProgress latest;
    int version = 0, completed = 0;
  };
  TextureLoadHooks hooks_;
  QTimer poll_;
  std::deque<Request> queue_;
  std::optional<Request> current_;
  std::shared_future<LoadedTexture> job_;
  std::shared_ptr<std::atomic_bool> cancel_;
  std::shared_ptr<Progress> progress_;
  int shown_version_ = -1;
  bool cancelled_shown_ = false, preparing_shown_ = false;
  std::chrono::steady_clock::time_point preparation_started_{};
  // One decode thread for the whole session. Creating it on the import click stalled the UI.
  std::mutex worker_mutex_;
  std::condition_variable worker_cv_;
  std::deque<std::function<void()>> worker_jobs_;
  bool worker_stop_ = false;
  std::future<void> worker_;
  void enqueue(Request);
  void next();
  void poll();
};
}  // namespace bts
