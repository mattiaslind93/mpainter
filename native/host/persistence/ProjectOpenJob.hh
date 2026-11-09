#pragma once
#include "OwnerWorkerJob.hh"
#include "PendingProjectGpu.hh"
#include "ProjectIO.hh"

namespace bts {
class BlenderHost;
struct ProjectOpenResult {
  std::unique_ptr<PendingProject> pending;
  std::unique_ptr<PendingProjectGpu> gpu;
  ProjectOpenResult() = default;
  ProjectOpenResult(ProjectOpenResult &&) = default;
  ProjectOpenResult &operator=(ProjectOpenResult &&other) noexcept
  {
    if (this != &other) {
      clear();
      pending = std::move(other.pending);
      gpu = std::move(other.gpu);
    }
    return *this;
  }
  ~ProjectOpenResult()
  {
    clear();
  }
  void clear() noexcept
  {
    gpu.reset();
    pending.reset();
  }  // GPU owner BEFORE BFD Main.
};
// One outer Open lifetime covers worker read, isolated owner GPU staging and
// final revision-guarded publication. Cancel never destroys a running future.
class ProjectOpenJob final : public QObject {
 public:
  using Reader = OwnerWorkerJob<std::unique_ptr<PendingProject>>;
  using Progress = Reader::Progress;
  using Work = Reader::Work;
  explicit ProjectOpenJob(BlenderHost &, QObject *parent = nullptr);
  ~ProjectOpenJob();
  bool pending() const
  {
    return active_;
  }
  void cancel();
  void start(Work,
             std::function<bool()> valid,
             std::function<void(ProjectOpenResult)> ready,
             Progress error,
             Progress progress = {},
             std::function<void()> finished = {},
             std::function<bool()> deliver_when = {});
  void shutdown() noexcept;  // Terminal owner/GL-alive drain, never pumps events.
 private:
  void cpu_ready(std::unique_ptr<PendingProject>);
  void poll();
  void complete();
  void fail(const QString &);
  void finish();
  BlenderHost &host_;
  Reader reader_;
  QTimer timer_;
  ProjectOpenResult result_;
  bool active_ = false, cancelled_ = false, ready_seen_ = false, terminal_ = false;
  std::function<bool()> valid_, deliver_when_;
  std::function<void(ProjectOpenResult)> ready_;
  Progress error_, progress_;
  std::function<void()> finished_;
};
}  // namespace bts
