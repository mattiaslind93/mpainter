#include <QtCore>
#include "ProjectOpenJob.hh"
#include "BlenderHost.hh"

namespace bts {
ProjectOpenJob::ProjectOpenJob(BlenderHost &host, QObject *parent)
    : QObject(parent), host_(host), reader_(this)
{
  timer_.setInterval(16);
  connect(&timer_, &QTimer::timeout, this, [this] { poll(); });
}
ProjectOpenJob::~ProjectOpenJob()
{
  shutdown();
}
void ProjectOpenJob::start(Work work,
                           std::function<bool()> valid,
                           std::function<void(ProjectOpenResult)> ready,
                           Progress error,
                           Progress progress,
                           std::function<void()> finished,
                           std::function<bool()> deliver_when)
{
  if (terminal_)
    throw std::logic_error("Project Open owner has shut down");
  if (active_)
    throw std::logic_error("Another project Open is still finishing");
  active_ = true;
  cancelled_ = ready_seen_ = false;
  valid_ = std::move(valid);
  ready_ = std::move(ready);
  error_ = std::move(error);
  progress_ = std::move(progress);
  finished_ = std::move(finished);
  deliver_when_ = std::move(deliver_when);
  try {
    reader_.start(
        std::move(work),
        [this] { return !cancelled_ && (!valid_ || valid_()); },
        [this](auto pending) { cpu_ready(std::move(pending)); },
        [this](const auto &message) { fail(message); },
        [this](const auto &message) {
          if (progress_)
            progress_(message);
        },
        [this] {
          if (active_ && !result_.pending)
            finish();
        },
        [this] { return !deliver_when_ || deliver_when_(); });
  }
  catch (...) {
    finish();
    throw;
  }
}
void ProjectOpenJob::cancel()
{
  cancelled_ = true;
  reader_.cancel();
  if (result_.pending)
    timer_.start();
}
void ProjectOpenJob::cpu_ready(std::unique_ptr<PendingProject> pending)
{
  result_.pending = std::move(pending);
  result_.pending->validate();
  if (cancelled_) {
    finish();
    return;
  }
  if (valid_ && !valid_())
    throw std::runtime_error("Project changed while opening; result discarded");
  result_.gpu = host_.begin_project_gpu(*result_.pending);
  if (!result_.gpu) {
    complete();
    return;
  }
  if (progress_)
    progress_("Preparing isolated project source GPU resources…");
  timer_.start();
}
void ProjectOpenJob::poll()
{
  if (!active_ || !result_.pending)
    return;
  // A nested paint event may temporarily own the viewport; never wait or clean
  // GPU resources reentrantly. Terminal shutdown drains after painting returns.
  if (!host_.project_gpu_available())
    return;
  try {
    if (cancelled_) {
      finish();
      return;
    }
    if (valid_ && !valid_())
      throw std::runtime_error("Project changed while opening; result discarded");
    if (deliver_when_ && !deliver_when_())
      return;
    if (ready_seen_) {
      complete();
      return;
    }
    if (host_.step_project_gpu(*result_.pending, *result_.gpu))
      ready_seen_ = true;
    // Publish on a separate owner tick so queued Cancel/selection/edit events
    // can invalidate a newly compiled result before any live project changes.
  }
  catch (const std::exception &error) {
    fail(QString::fromUtf8(error.what()));
  }
  catch (...) {
    fail("Project Open GPU staging failed with an unknown native error");
  }
}
void ProjectOpenJob::complete()
{
  auto ready = std::move(ready_);
  auto result = std::move(result_);
  try {
    if (ready)
      ready(std::move(result));
    finish();
  }
  catch (const std::exception &error) {
    fail(QString::fromUtf8(error.what()));
  }
  catch (...) {
    fail("Project Open publication failed with an unknown native error");
  }
}
void ProjectOpenJob::fail(const QString &message)
{
  auto error = std::move(error_);
  const bool notify = !cancelled_;
  finish();
  if (notify && error)
    error(message);
}
void ProjectOpenJob::finish()
{
  timer_.stop();
  result_.clear();
  active_ = false;
  ready_seen_ = false;
  valid_ = {};
  deliver_when_ = {};
  ready_ = {};
  error_ = {};
  progress_ = {};
  auto finished = std::move(finished_);
  if (finished)
    finished();
}
void ProjectOpenJob::shutdown() noexcept
{
  terminal_ = true;
  cancelled_ = true;
  timer_.stop();
  ready_ = {};
  error_ = {};
  progress_ = {};
  finished_ = {};
  valid_ = {};
  deliver_when_ = {};
  reader_.shutdown();
  result_.clear();
  active_ = false;
}
}  // namespace bts
