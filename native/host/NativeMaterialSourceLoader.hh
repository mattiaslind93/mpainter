#pragma once
#include "BlendMaterial.hh"
#include <QObject>
#include <QTimer>
#include <functional>
#include <future>
#include <map>
namespace bts {
class BlenderHost;
// UI-owner coordinator. Worker decoding and frame-stepped GPU preparation must
// both finish before a caller may publish a Shelf asset or mutate a document.
class NativeMaterialSourceLoader : public QObject {
 public:
  using Ready = std::function<void(const StudioAsset &)>;
  using Failed = std::function<void(const QString &)>;
  NativeMaterialSourceLoader(BlenderHost &,
                             QObject *parent,
                             std::function<void()> redraw,
                             std::function<void(const QString &)> progress);
  ~NativeMaterialSourceLoader();
  void ensure(const StudioAsset &,
              std::vector<StudioAsset> textures,
              Ready,
              Failed,
              bool require_preview = false);
  void prepared(PreparedBlendMaterial, Ready, Failed);
  void cancel(const QUuid &);
  void cancel_all();
  bool pending() const
  {
    return !jobs_.empty();
  }

 private:
  struct Completion {
    Ready ready;
    Failed failed;
  };
  struct Job {
    StudioAsset asset;
    std::shared_ptr<std::atomic_bool> cancel = std::make_shared<std::atomic_bool>(false);
    std::shared_future<std::shared_ptr<NativeMaterialGraph>> worker;
    std::shared_future<StudioAsset> preview_archive;
    std::vector<Completion> completions;
    bool queued = false;
  };
  void poll();
  BlenderHost &host_;
  QTimer timer_;
  std::function<void()> redraw_;
  std::function<void(const QString &)> progress_;
  std::map<QUuid, Job> jobs_;
};
}  // namespace bts
