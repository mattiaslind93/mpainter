#pragma once
#include "NativeMaterialHeightGpu.hh"
#include "NativeMaterialHeightResource.hh"
#include "AssetCatalog.hh"
#include "NativeMaterialParameters.hh"
#include "OwnerWorkerJob.hh"
#include <QUuid>
namespace bts {
class BlenderHost;
struct NativeHeightUiTarget {
  QUuid document, layer;
  std::uint32_t material_uid = 0;
  NativeMaterialDescription instance;
  StudioAsset source;
  std::vector<StudioAsset> resources;
  std::function<bool()> current;
};
struct NativeHeightUiHooks {
  std::function<void(NativeMaterialHeightGpu *, NativeHeightPolicy, const NativeHeightUiTarget &)>
      publish;
  std::function<void(const QString &)> progress, error;
  std::function<void()> finished;
};
// Explicit selected-instance policy only. Browsing/synchronizing never prepares
// a source. The terminal owner drain precedes Host/GL/global allocator teardown.
class NativeMaterialHeightUi final : public QObject {
 public:
  NativeMaterialHeightUi(BlenderHost &, NativeHeightUiHooks, QObject *parent = nullptr);
  ~NativeMaterialHeightUi();
  bool pending() const
  {
    return active_;
  }
  void request(NativeHeightUiTarget, NativeHeightPolicy);
  void cancel();
  void shutdown() noexcept;

 private:
  void poll();
  void finish();
  void fail(const QString &);
  BlenderHost &host_;
  NativeHeightUiHooks hooks_;
  OwnerWorkerJob<std::unique_ptr<NativeMaterialHeightResource>> worker_;
  QTimer timer_;
  NativeHeightUiTarget target_;
  std::unique_ptr<NativeMaterialHeightResource> resource_;
  std::unique_ptr<NativeMaterialHeightGpu> gpu_;
  NativeHeightPolicy policy_ = NativeHeightPolicy::None;
  bool active_ = false, cancelled_ = false, terminal_ = false;
};
}  // namespace bts
