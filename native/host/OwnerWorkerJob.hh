#pragma once
#include <QObject>
#include <QTimer>
#include <QString>
#include <atomic>
#include <chrono>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <stdexcept>

namespace bts {
// One worker at a time. Completed results, including rejected detached native
// graphs, are consumed/destroyed on this object's owner. Cancellation never
// drops a future and therefore never joins a worker inside a UI event.
template<class Result> class OwnerWorkerJob : public QObject {
 public:
  using Cancel = std::shared_ptr<std::atomic_bool>;
  using Progress = std::function<void(const QString &)>;
  using Work = std::function<Result(Cancel, Progress)>;
  explicit OwnerWorkerJob(QObject *owner = nullptr) : QObject(owner)
  {
    timer_.setInterval(16);
    connect(&timer_, &QTimer::timeout, this, [this] { poll(); });
  }
  ~OwnerWorkerJob()
  {
    shutdown();
  }
  bool pending() const
  {
    return worker_.valid();
  }
  void cancel()
  {
    if (state_)
      state_->cancel->store(true);
  }
  void start(Work work,
             std::function<bool()> valid,
             std::function<void(Result)> ready,
             Progress error,
             Progress progress = {},
             std::function<void()> finished = {},
             std::function<bool()> deliver_when = {})
  {
    if (pending())
      throw std::logic_error("Another project Open is still finishing");
    state_ = std::make_shared<State>();
    valid_ = std::move(valid);
    ready_ = std::move(ready);
    error_ = std::move(error);
    progress_ = std::move(progress);
    finished_ = std::move(finished);
    deliver_when_ = std::move(deliver_when);
    worker_ = std::async(std::launch::async, [work = std::move(work), state = state_]() mutable {
      return work(state->cancel, [state](const QString &text) {
        std::lock_guard lock(state->mutex);
        state->text = text;
      });
    });
    timer_.start();
  }
  void poll()
  {
    if (!pending())
      return;
    QString text;
    {
      std::lock_guard lock(state_->mutex);
      text = state_->text;
    }
    if (!state_->cancel->load() && progress_ && !text.isEmpty())
      progress_(text);
    if (worker_.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
      return;
    if (!state_->cancel->load() && deliver_when_ && !deliver_when_() && (!valid_ || valid_()))
      return;
    timer_.stop();
    deliver_when_ = {};
    auto state = std::move(state_);
    auto valid = std::move(valid_);
    auto ready = std::move(ready_);
    auto error = std::move(error_);
    progress_ = {};
    struct Finish {
      std::function<void()> callback;
      ~Finish()
      {
        if (callback)
          callback();
      }
    } finish{std::move(finished_)};
    try {
      auto result = worker_.get();  // Ready only; no worker wait on the owner.
      if (state->cancel->load())
        return;
      if (valid && !valid())
        throw std::runtime_error("Project changed while opening; result discarded");
      ready(std::move(result));
    }
    catch (const std::exception &e) {
      if (!state->cancel->load() && error)
        error(QString::fromUtf8(e.what()));
    }
    catch (...) {
      if (!state->cancel->load() && error)
        error("Project Open failed with an unknown native error");
    }
  }
  // Application shutdown only: finish ownership transfer before Blender's
  // registries/allocators go away. No native graph outlives the owner session.
  void shutdown()
  {
    cancel();
    timer_.stop();
    if (worker_.valid()) {
      try {
        auto retired = worker_.get();
      }
      catch (...) {
      }
    }
    state_.reset();
    ready_ = {};
    valid_ = {};
    error_ = {};
    progress_ = {};
    finished_ = {};
    deliver_when_ = {};
  }

 private:
  struct State {
    Cancel cancel = std::make_shared<std::atomic_bool>(false);
    std::mutex mutex;
    QString text;
  };
  QTimer timer_;
  std::future<Result> worker_;
  std::shared_ptr<State> state_;
  std::function<bool()> valid_;
  std::function<void(Result)> ready_;
  Progress error_, progress_;
  std::function<void()> finished_;
  std::function<bool()> deliver_when_;
};
}  // namespace bts
