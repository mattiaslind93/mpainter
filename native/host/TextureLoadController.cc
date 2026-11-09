#include "TextureLoadController.hh"
#include "ImageSequence.hh"
#include "Performance.hh"
#include <QFileInfo>
#include <chrono>
namespace bts {
TextureLoadController::TextureLoadController(QObject *parent, TextureLoadHooks hooks)
    : QObject(parent), hooks_(std::move(hooks))
{
  setObjectName("textureLoadController");
  poll_.setInterval(16);
  connect(&poll_, &QTimer::timeout, this, [this] { poll(); });
  worker_ = std::async(std::launch::async, [this] {
    for (;;) {
      std::function<void()> job;
      {
        std::unique_lock lock(worker_mutex_);
        worker_cv_.wait(lock, [this] { return worker_stop_ || !worker_jobs_.empty(); });
        if (worker_stop_ && worker_jobs_.empty())
          return;
        job = std::move(worker_jobs_.front());
        worker_jobs_.pop_front();
      }
      job();
    }
  });
}
TextureLoadController::~TextureLoadController()
{
  cancel_all();
  if (job_.valid())
    job_.wait();
  {
    std::lock_guard lock(worker_mutex_);
    worker_stop_ = true;
  }
  worker_cv_.notify_one();
  if (worker_.valid())
    worker_.wait();
}
void TextureLoadController::from_file(TextureLoadTarget target,
                                      const QString &path,
                                      const std::string &source_space)
{
  enqueue({std::move(target), path, {}, source_space});
}
void TextureLoadController::from_asset(TextureLoadTarget target,
                                       StudioAsset asset,
                                       const std::string &space)
{
  if (asset.scope != AssetScope::Project) {
    asset.scope = AssetScope::Project;
    asset.id = QUuid::createUuid();
  }
  enqueue({std::move(target), {}, std::move(asset), space});
}
void TextureLoadController::cancel(const TextureLoadTarget &target)
{
  std::erase_if(queue_, [&](const auto &request) {
    if (!request.target.same_slot(target))
      return false;
    hooks_.status(request.target, {"Cancelled", 0, 0, false, false});
    return true;
  });
  if (current_ && current_->target.same_slot(target) && cancel_) {
    cancel_->store(true);
    cancelled_shown_ = true;
    hooks_.status(current_->target, {"Cancelled", 0, 0, false, false});
  }
}
void TextureLoadController::cancel_all()
{
  if (hooks_.release_preparation)
    hooks_.release_preparation();
  for (const auto &request : queue_)
    hooks_.status(request.target, {"Cancelled", 0, 0, false, false});
  queue_.clear();
  if (cancel_) {
    cancel_->store(true);
    cancelled_shown_ = true;
    if (current_)
      hooks_.status(current_->target, {"Cancelled", 0, 0, false, false});
  }
}
void TextureLoadController::enqueue(Request request)
{
  cancel(request.target);
  hooks_.status(request.target, {"Queued", 0, 0, true, false});
  queue_.push_back(std::move(request));
  next();
  poll_.start();
}
void TextureLoadController::next()
{
  if (job_.valid() || queue_.empty())
    return;
  current_ = std::move(queue_.front());
  queue_.pop_front();
  cancel_ = std::make_shared<std::atomic_bool>(false);
  progress_ = std::make_shared<Progress>();
  shown_version_ = -1;
  cancelled_shown_ = false;
  preparing_shown_ = false;
  preparation_started_ = {};
  const auto request = *current_;
  auto cancel = cancel_;
  auto progress = progress_;
  hooks_.status(request.target, {"Loading…", 0, 0, true, false});
  auto promise = std::make_shared<std::promise<LoadedTexture>>();
  job_ = promise->get_future().share();
  {
    std::lock_guard lock(worker_mutex_);
    worker_jobs_.push_back([promise, request, cancel, progress] {
      try {
        const auto started = performance::Clock::now();
        LoadedTexture result;
        result.target = request.target;
        if (request.asset)
          result.asset = *request.asset;
        else {
          const auto source = reference_image_resource(
              request.path, true, cancel.get(), [&](int index, int total, const QString &name) {
                std::lock_guard lock(progress->mutex);
                progress->latest = {0, index, total, name, "Reading source"};
                ++progress->version;
              });
          result.asset.name = source.name;
          result.asset.filename = source.filename;
          result.asset.path = source.path;
          result.asset.texture_files = source.files;
          result.asset.usages = {"texture"};
          result.asset.scope = AssetScope::Project;
        }
        const auto &asset = result.asset;
        std::set<int> tiles;
        if (!asset.texture_files.empty() && asset.texture_files.front().tile)
          for (const auto &file : asset.texture_files)
            tiles.insert(file.tile);
        else if (is_image_sequence(asset.bytes))
          for (const auto &part : read_image_sequence(asset.bytes))
            tiles.insert(part.tile);
        if (!tiles.empty() && request.target.tiles.size() > 1)
          for (int tile : request.target.tiles)
            if (!tiles.contains(tile))
              throw std::runtime_error((QString("tile %1: required UV image is missing from %2")
                                            .arg(tile)
                                            .arg(asset.name))
                                           .toStdString());
        TextureDecodeControl control;
        control.cancel = cancel.get();
        control.verify_files = request.asset.has_value();
        control.progress = [progress](const TextureDecodeProgress &value) {
          std::lock_guard lock(progress->mutex);
          progress->latest = value;
          if (value.phase == "Ready")
            ++progress->completed;
          ++progress->version;
        };
        result.texture = decode_fill_texture(asset,
                                             request.target.channel != 0 ||
                                                 !request.target.effect.isNull(),
                                             request.source_space,
                                             control);
        result.worker_ms = performance::ms(started);
        promise->set_value(std::move(result));
      }
      catch (...) {
        promise->set_exception(std::current_exception());
      }
    });
  }
  worker_cv_.notify_one();
}
void TextureLoadController::poll()
{
  if (!job_.valid()) {
    next();
    if (!job_.valid())
      poll_.stop();
    return;
  }
  if (current_ && !hooks_.valid(current_->target) && !cancel_->load()) {
    cancel_->store(true);
    cancelled_shown_ = true;
    hooks_.status(current_->target, {"Target changed — cancelled", 0, 0, false, false});
  }
  if (!cancel_->load()) {
    TextureDecodeProgress progress;
    int completed = 0, version = 0;
    {
      std::lock_guard lock(progress_->mutex);
      version = progress_->version;
      progress = progress_->latest;
      completed = progress_->completed;
    }
    if (version != shown_version_) {
      shown_version_ = version;
      hooks_.status(current_->target,
                    {QString("Loading %1/%2 (%3)…")
                         .arg(std::min(progress.index + 1, progress.total))
                         .arg(progress.total)
                         .arg(progress.filename),
                     completed,
                     progress.total,
                     true,
                     false});
    }
  }
  if (job_.wait_for(std::chrono::seconds(0)) != std::future_status::ready ||
      (!cancel_->load() && hooks_.busy()))
    return;
  const auto target = current_->target;
  const bool cancelled = cancel_->load();
  try {
    auto result = job_.get();
    if (!cancelled && hooks_.valid(target)) {
      if (preparation_started_ == performance::Clock::time_point{})
        preparation_started_ = performance::Clock::now();
      if (hooks_.prepare && !hooks_.prepare(result)) {
        if (!preparing_shown_) {
          hooks_.status(target, {"Preparing texture…", 0, 1, true, false});
          preparing_shown_ = true;
        }
        return;
      }
      const double prepare_ms = performance::ms(preparation_started_);
      const auto started = performance::Clock::now();
      const auto name = result.asset.name;
      const auto worker_ms = result.worker_ms;
      {
        performance::Scope timing("texture_install");
        hooks_.install(std::move(result));
      }
      {
        performance::Scope timing("texture_status_complete");
        hooks_.status(target, {name, 1, 1, false, false});
      }
      std::printf(
          "BTS_TEXTURE_ASSIGN_MS worker_ms=%.3f prepare_ms=%.3f owner_ms=%.3f channel=%d\n",
          worker_ms,
          prepare_ms,
          performance::ms(started),
          target.channel);
    }
  }
  catch (const std::exception &error) {
    if (!cancelled) {
      const auto label = current_->asset ? current_->asset->name :
                                           QFileInfo(current_->path).fileName();
      const auto message = label + ": " + QString::fromUtf8(error.what());
      hooks_.status(target, {message, 0, 0, false, true});
      hooks_.error(message);
    }
  }
  if (cancelled && !cancelled_shown_)
    hooks_.status(target, {"Cancelled", 0, 0, false, false});
  if (hooks_.release_preparation)
    hooks_.release_preparation();
  job_ = {};
  current_.reset();
  cancel_.reset();
  progress_.reset();
  next();
  if (!job_.valid())
    poll_.stop();
}
}  // namespace bts
