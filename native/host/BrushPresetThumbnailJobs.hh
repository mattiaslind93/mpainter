#pragma once
#include "AssetCatalog.hh"
#include <QImage>
#include <QJsonObject>
#include <functional>
#include <memory>
namespace bts {
class BlenderHost;
// Owner-thread GPU stroke renderer. Smudge/Clone/Erase show the neutral brush
// footprint, never scene transport. No document or saved brush is mutated.
class BrushPresetThumbnailJobs {
 public:
 BrushPresetThumbnailJobs(BlenderHost &,std::function<bool()> ready,std::function<void(QString)> error);
 ~BrushPresetThumbnailJobs();
 void prepare_startup(); // Host GPU startup, before timed interactive requests.
 void request(const StudioAsset &,std::function<void(QImage)>);
 bool pending()const;
 QJsonObject diagnostics()const; // Empty unless BTS_BO_PRESET_DEMO is enabled.
 void shutdown(); // Owner thread, before BlenderHost shutdown and outside draw.
 private:
 struct Impl;std::shared_ptr<Impl> impl_;
};
}
