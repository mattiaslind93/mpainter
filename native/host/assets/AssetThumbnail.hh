#pragma once
#include "AssetCatalog.hh"
#include <QImage>
#include <functional>
class QObject;
namespace bts {
QImage asset_thumbnail(const StudioAsset &);
// Missing native previews are requested on the owner after worker inspection.
void asset_thumbnail_native_generator(
    QObject *, std::function<void(const StudioAsset &, std::function<void(QImage)>)>);
void asset_thumbnail_ready(QObject *, std::function<void()>);
void stop_asset_thumbnail_jobs();
}  // namespace bts
