// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "NativeMaterialImageJobs.hh"
#include <QPointer>
class QWidget;
namespace bts {
class BlenderHost;
class NativeMaterialImagePanel;
struct NativeImageUiTarget {
  QUuid document, layer;
  std::uint32_t material_uid = 0;
  StudioAsset source;
  std::vector<StudioAsset> resources;
  NativeMaterialDescription instance;
  // Captures document/revision/selection/source/authored state on the UI owner.
  // Never passed to a worker.
  std::function<bool()> current;
};
struct NativeImageUiHooks {
  std::function<std::optional<NativeImageUiTarget>()> target;
  std::function<void(NativeImageChange, const QUuid &, const NativeImageUiTarget &, bool reset)>
      publish;
  std::function<void()> shelf, redraw;
  std::function<void(const QString &)> status, error;
};
class NativeMaterialImageUi final : public QObject {
 public:
  NativeMaterialImageUi(QWidget *, NativeMaterialImagePanel *, BlenderHost &, NativeImageUiHooks);
  ~NativeMaterialImageUi();
  bool consume_shelf(const StudioAsset &);
  void cancel();
  bool pending() const
  {
    return bool(active_) || bool(shelf_) || (jobs_ && jobs_->pending());
  }

 private:
  void refresh();
  void choose(const NativeImageBinding &, bool file, const NativeImageReplacementPolicy &);
  void begin(NativeImageChangeRequest, NativeImageUiTarget);
  void reset();
  void error(const QString &);
  QPointer<QWidget> owner_;
  QPointer<NativeMaterialImagePanel> panel_;
  NativeImageUiHooks hooks_;
  std::unique_ptr<NativeMaterialImageJobs> jobs_;
  struct Pending {
    NativeImageChangeRequest request;
    NativeImageUiTarget target;
  };
  std::optional<Pending> shelf_;
  std::optional<NativeImageUiTarget> active_;
  bool reset_all_ = false;
  std::uint64_t serial_ = 0;
  bool shelf_cancelled_ = false;
};
}  // namespace bts
