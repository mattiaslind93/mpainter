// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "OpenPbrImportPolicy.hh"
#include "OpenPbrUsdResources.hh"
#include "FillTextureIO.hh"
#include "UsdScene.hh"
#include <QObject>
#include <QTimer>
#include <condition_variable>
#include <deque>
#include <future>
#include <mutex>
namespace bts {
struct OpenPbrMaterialImportRequest {
  QUuid document;
  std::uint32_t material_uid = 0;
  bool uv_tiles = false;
  QString source, material_path, cache;
  UsdImportOptions options;
  std::set<int> tiles;
  OpenPbrParameters expected;
};
struct PreparedOpenPbrMaterial {
  OpenPbrMaterialImportRequest request;
  OpenPbrUsdProfile source;
  OpenPbrImportConstants constants;
  double geometry_scale = 1;
  std::vector<StudioAsset> assets;
  std::map<int, std::shared_ptr<const FillTexture>> textures;
};
PreparedOpenPbrMaterial prepare_openpbr_material(const OpenPbrMaterialImportRequest &,
                                                 const std::atomic_bool &,
                                                 const std::function<void(const QString &)> &);
// Structural layer edits only. GPU composition is performed by the caller.
void apply_openpbr_import_fill(TextureSet &, const PreparedOpenPbrMaterial &);
struct OpenPbrMaterialImportHooks {
  std::function<bool(const OpenPbrMaterialImportRequest &)> valid;
  std::function<bool()> busy;
  std::function<void(const QString &)> status;
  std::function<bool(const PreparedOpenPbrMaterial &)> prepare;
  std::function<void(PreparedOpenPbrMaterial)> install;
  std::function<void()> release;
  std::function<void(const QString &)> error;
};
class OpenPbrMaterialImporter final : public QObject {
 public:
  OpenPbrMaterialImporter(QObject *, OpenPbrMaterialImportHooks);
  ~OpenPbrMaterialImporter();
  void start(std::vector<OpenPbrMaterialImportRequest>);
  void cancel();
  bool pending() const
  {
    return future_.valid() || !queue_.empty();
  }

 private:
  OpenPbrMaterialImportHooks hooks_;
  QTimer timer_;
  std::deque<OpenPbrMaterialImportRequest> queue_;
  std::optional<OpenPbrMaterialImportRequest> current_;
  std::future<PreparedOpenPbrMaterial> future_;
  std::shared_ptr<std::atomic_bool> cancel_;
  std::future<void> worker_;
  std::mutex mutex_;
  std::condition_variable condition_;
  std::deque<std::function<void()>> tasks_;
  bool stop_ = false;
  struct Progress {
    std::mutex mutex;
    QString text;
  };
  std::shared_ptr<Progress> progress_;
  QString shown_;
  void next();
  void poll();
};
}  // namespace bts
