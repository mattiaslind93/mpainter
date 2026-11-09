// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "MaterialLayerParameterDocument.hh"
#include "AssetCatalog.hh"
#include "OwnerWorkerJob.hh"
#include <QJsonObject>
#include <optional>

namespace bts {
struct MaterialParameterImportTarget {
  QUuid project, set;
  std::uint32_t uid = 0;
  std::uint64_t revision = 0;
  QString working_space;
  std::array<double, 9> scene_linear_to_xyz{};
  QJsonObject signature;  // Cached edit metadata only; no owner-side pixel serialization.
};
struct MaterialParameterImportCandidate {
  MaterialParameterImportTarget target;
  std::unique_ptr<TextureDocument> document;
  std::vector<StudioAsset> originals;
};
struct MaterialParameterImportStatus {
  bool pending = false, cancelled = false;
  QString text, error;
};
struct MaterialParameterImportHooks {
  std::function<bool(const MaterialParameterImportTarget &)> current;
  std::function<void(const MaterialParameterImportTarget &)> validate;  // Real owner/native gate.
  std::function<bool()> busy;
  std::function<bool(const QUuid &)> claim;
  std::function<void(const QUuid &)> release;
  std::function<bool(MaterialParameterImportCandidate &, const QUuid &)> publish;
};
// One retained worker followed by an exclusive owner preparation lease. Cancel
// always consumes a ready worker result; publication readiness never gates it.
class MaterialLayerParameterImportJob final : public QObject {
 public:
  explicit MaterialLayerParameterImportJob(MaterialParameterImportHooks,
                                           QObject *parent = nullptr);
  ~MaterialLayerParameterImportJob() override;
  void start(MaterialParameterImportTarget, TextureDocument, const QString &directory);
  void cancel();
  bool pending() const
  {
    return active_;
  }
  MaterialParameterImportStatus status() const
  {
    return {active_, cancelled_, text_, error_};
  }
  void shutdown() noexcept;  // Terminal only, before Save event pumping and Host close.
 private:
  void poll();
  void release();
  void finish();
  void fail(const QString &);
  MaterialParameterImportHooks hooks_;
  OwnerWorkerJob<MaterialParameterImportCandidate> reader_;
  QTimer timer_;
  std::optional<MaterialParameterImportCandidate> candidate_;
  MaterialParameterImportTarget target_;
  QUuid token_;
  bool active_ = false, cancelled_ = false, leased_ = false, terminal_ = false;
  QString text_, error_;
};
}  // namespace bts
