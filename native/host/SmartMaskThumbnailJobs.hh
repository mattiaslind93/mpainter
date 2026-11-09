#pragma once
#include "AssetCatalog.hh"
#include <QImage>
#include <functional>
#include <memory>
namespace bts {
class BlenderHost;
// Request-driven GPU mask evaluation on our own synthetic sphere. No document
// stack readback, source preview substitution or CPU mask evaluation occurs.
class SmartMaskThumbnailJobs {
 public:
  SmartMaskThumbnailJobs(BlenderHost &,
                         std::function<bool()> ready,
                         std::function<void(QString)> error);
  ~SmartMaskThumbnailJobs();
  void prepare_startup();  // Owner GPU initialization, before interactive request timers.
  void request(const StudioAsset &, std::function<void(QImage)>);
  void shutdown();  // Before BlenderHost shutdown; idle call, never inside a draw.
  bool pending() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
}  // namespace bts
